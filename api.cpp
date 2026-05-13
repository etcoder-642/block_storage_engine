#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <cstring>
namespace fs = std::filesystem;

#include "api.h"

using namespace std;

// HELPER FUNCTIONS
// returns a specific directory where disk will be stored (e.g. "/.local/share/BlockEngine/" in Linux)
string getAppDirectory()
{
#ifdef _WIN32
    // C:\Users\name\AppData\Roaming\BlockEngine\
    const char* appData = getenv("APPDATA");
    if (!appData)
        throw runtime_error("APPDATA environment variable not found");
    return string(appData) + "\\BlockEngine\\";

#elif __APPLE__
    // /Users/name/Library/Application Support/BlockEngine/
    const char *home = getenv("HOME");
    if (!home)
        throw runtime_error("HOME environment variable not found");
    return string(home) + "/Library/Application Support/BlockEngine/";

#else
    // Linux: /home/name/.local/share/BlockEngine/
    // Respects XDG standard if set, falls back to HOME
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg)
        return string(xdg) + "/BlockEngine/";

    const char *home = getenv("HOME");
    if (!home)
        throw runtime_error("HOME environment variable not found");
    return string(home) + "/.local/share/BlockEngine/";
#endif
}

vector<string> parseString(string str, char delimiter)
{
    vector<string> res;
    string section;
    for (size_t i = 0; i < str.size(); i++)
    {
        if (str[i] == delimiter)
        {
            res.push_back(section);
            section = "";
            continue;
        }
        section += str[i];
    }
    if (!section.empty())
        res.push_back(section);
    return res;
}

string joinString(vector<string> vstr, char delimiter)
{
    string res = "";
    for (size_t i = 0; i < vstr.size(); i++)
    {
        res += vstr[i];
        res += delimiter;
    }
    return res;
}

bool findFileInDirectory(const string &fileName, const string &dirPath)
{
    fs::path needle = fs::path(dirPath) / (fileName + SuperBlock::DISK_EXTENSION);
    return fs::exists(needle);
}

// BITMAP OPERATIONS
bool BlockStorageEngine::isBlockFree(int bitPosition)
{
    disk.clear();
    int maskingIndex = bitPosition % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    unsigned char mask = 1 << maskingIndex;
    disk.seekg(sb.bitmapStart + (bitPosition / 8), ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    disk.seekg(0, ios::beg); // Reset position after reading
    if (bitnum & mask)
    {
        return false;
    }
    else
        return true;
}

// flips the bit at bitPosition in the bitmap byte to mark block as occupied
void BlockStorageEngine::setBitOccupied(int bitPosition)
{
    disk.clear();
    int maskingPos = bitPosition % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    short mask = 1 << maskingPos;
    disk.seekg(sb.bitmapStart + (bitPosition / 8), ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    bitnum |= mask;
    disk.seekp(sb.bitmapStart + (bitPosition / 8), ios::beg);
    disk.write(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    disk.flush(); // Ensure the updated bitmap is written to disk
}

void BlockStorageEngine::setBitFree(int bitPosition)
{
    disk.clear();
    int maskingPos = bitPosition % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    short mask = 1 << maskingPos;
    disk.seekg(sb.bitmapStart + (bitPosition / 8), ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    bitnum &= ~mask;
    disk.seekp(sb.bitmapStart + (bitPosition / 8), ios::beg);
    disk.write(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    disk.flush(); // Ensure the updated bitmap is written to disk
}

// DISK GEOMERTY AND FORMATTING
// calculates the total size of the drive
long BlockStorageEngine::calculateTotalSize()
{
    long totalSize = sizeof(SuperBlock) + sb.blockSize * sb.blockCount; // Size of all blocks
    totalSize += sb.bitmapSize;                                         // Add bitmap size
    totalSize += sizeof(Inode) * sb.inodeCount;                         // Add size of all inodes
    return totalSize;
}

// fills entire disk with 0's
void BlockStorageEngine::formatDisk(int size, int initialPos)
{
    disk.seekp(initialPos, ios::beg);
    char zero[sb.blockSize] = {0};
    for (long i = 0; i < size / sb.blockSize; i++)
    {
        disk.write(reinterpret_cast<char *>(&zero), sb.blockSize);
    }
}

// syncs in-memory superBlock data with the disk
void BlockStorageEngine::syncSuperBlock()
{
    disk.seekp(0, ios::beg);
    disk.write(reinterpret_cast<char *>(&sb), sizeof(SuperBlock));
    disk.flush();
}

// recalculates all superblock fields — because properties are interdependent
void BlockStorageEngine::updateSbInfo()
{
    sb.bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
    sb.bitmapSize = sb.blockCount / 8;
    sb.inodeTableStart = sb.bitmapStart + sb.bitmapSize;
    sb.dataRegionStart = sb.inodeTableStart + (sizeof(Inode) * sb.inodeCount);
    sb.freeBlockCount = sb.blockCount;

    sb.inodeTableStart = sb.bitmapStart + sb.bitmapSize;
    sb.dataRegionStart = sb.inodeTableStart + (sizeof(Inode) * sb.inodeCount);
}

// ALLOCATION
int BlockStorageEngine::findFreeBlock()
{
    int index = 0;

    // "Skip" all blocks that are already taken
    while (!isBlockFree(index))
    {
        index++;
        if (index >= sb.blockCount)
        {
            return -1;
        }
    }

    setBitOccupied(index);
    return index;
}

int BlockStorageEngine::findFreeInode()
{
    for (size_t i = 0; i < sb.inodeCount; i++)
    {
        Inode in = readInode(i);
        if (!in.isAllocated)
        {
            return i;
        }
    }
    if (sb.allocatedInodeCount >= sb.inodeCount)
        return -1;
    return sb.allocatedInodeCount; // return next free slot
}

int BlockStorageEngine::findFreeDirEntry(const int *directBlocks, int blockCount)
{
    int index = 0;
    for (int i = 0; i < blockCount; i++)
    {
        for (int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent;
            long pos = sb.dataRegionStart + (directBlocks[i] * sb.blockSize) + (j * sizeof(DirectoryEntry));
            disk.seekg(pos, ios::beg);
            disk.read(reinterpret_cast<char *>(&ent), sizeof(DirectoryEntry));
            if (!ent.isAllocated)
            {
                return index;
            }
            index++;
        }
    }
    return -1;
}

void BlockStorageEngine::freeDirectoryEntry(int dirEntryIndex, int dirInodeIndex)
{
    Inode in = readInode(dirInodeIndex);
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);

    int dirEntBlock = dirEntryIndex / dirEntryPerBlock;
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;
    int blockIndex = in.directBlocks[dirEntBlock];

    DirectoryEntry ent = readDirectoryEntry(dirEntOffset, blockIndex);
    ent.isAllocated = false;
    ent.inodeIndex = -1;
    ent.fileName[0] = '\0';
    writeDirEntry(ent, dirEntOffset, blockIndex);
}

void BlockStorageEngine::freeBlock(int index)
{
     if (index < 0 || index >= sb.blockCount)
    {
        cout << "freeBlock | Error: Invalid Block Index!" << endl;
        return;
    }
    int pos = sb.dataRegionStart + (index * sb.blockSize);
    formatDisk(sb.blockSize, pos);
    setBitFree(index);
    sb.freeBlockCount++;
}

void BlockStorageEngine::freeIndirectBlocks(int index, int usedCount)
{
    if (index < 0 || index >= sb.blockCount)
    {
        cout << "freeIndirectBlocks | Error: Invalid Block Index!" << endl;
        return;
    }
    int pos = sb.dataRegionStart + (index * sb.blockSize);
    int intsInBlock = sb.blockSize / sizeof(int);
    int blockIds[intsInBlock];
    disk.seekg(sb.dataRegionStart + (index * sb.blockSize), ios::beg);
    disk.read(reinterpret_cast<char *>(&blockIds), sb.blockSize);
    for (int i = 0; i < usedCount; i++)
    {
        freeBlock(blockIds[i]);
    }
    formatDisk(sb.blockSize, pos);
}

void BlockStorageEngine::allocateBlock(Inode &in, vector<char> &data)
{
    disk.clear();

    if (in.isDirectory)
    {
        cerr << "allocateBlock | Error: Cannot allocate blocks for a directory!" << endl;
        return;
    }

    char *dataPtr = data.data();
    int bytesRemaining = data.size();
    long writeLocation = 0;
    int blockSize = sb.blockSize;
    while (bytesRemaining > 0)
    {
        int amountToWrite = (blockSize < bytesRemaining) ? blockSize : bytesRemaining;

        int freeBlockID = this->findFreeBlock();
        if (freeBlockID == -1)
        {
            cerr << "allocateBlock | Error: Disk is full, cannot allocate more blocks!" << endl;
            return;
        }

        if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
        {
            in.indirectBlocks = freeBlockID;
            sb.freeBlockCount--;
            allocateIndirectBlock(in, data, bytesRemaining);
            return;
        }
        writeDataToBlock(freeBlockID, dataPtr, amountToWrite, bytesRemaining);
        dataPtr += amountToWrite;
        in.blockCount++;
        in.lastBlockUsedBytes = amountToWrite;
        in.directBlocks[in.blockCount - 1] = freeBlockID;
    }
    disk.flush();
    disk.clear();
}

void BlockStorageEngine::allocateIndirectBlock(Inode &in, vector<char> &data, int bytesRemaining)
{
    char *dataPtr = data.data() + (data.size() - (size_t)bytesRemaining);
    int blockSize = sb.blockSize;
    long writeLocation = 0;
    while (bytesRemaining > 0)
    {
        int amountToWrite = (blockSize < bytesRemaining) ? blockSize : bytesRemaining;

        int freeBlockID = this->findFreeBlock();
        if (freeBlockID == -1)
        {
            cerr << "allocateIndirectBlock | Error: Disk is full, cannot allocate more blocks!" << endl;
            return;
        }

        int blocksInTheIndirectBlock = in.blockCount - Inode::MAX_DIRECT_BLOCKS;
        if (blocksInTheIndirectBlock >= sb.blockSize / sizeof(int))
        {
            cout << "allocateIndirectBlock | Error: indirect block full, file too large!" << endl;
            return;
        }
        in.blockCount++;

        // go to the block pointed by the indirect Block and write the freeBlock we just discovered
        writeLocation = sb.dataRegionStart + (in.indirectBlocks * sb.blockSize) + (blocksInTheIndirectBlock * sizeof(int));
        disk.seekp(writeLocation, ios::beg);
        disk.write(reinterpret_cast<char *>(&freeBlockID), sizeof(int));

        // now we continue writing our data
        in.lastBlockUsedBytes = amountToWrite;
        writeDataToBlock(freeBlockID, dataPtr, amountToWrite, bytesRemaining);
        dataPtr += amountToWrite;
    }
    disk.flush();
}

// READ FROM DISK
Inode BlockStorageEngine::readInode(int inodeIndex)
{
    Inode myInode;
    long pos = sb.inodeTableStart + (inodeIndex * sizeof(Inode));
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myInode), sizeof(Inode));
    disk.clear();
    return myInode;
}

vector<char> BlockStorageEngine::recoverFile(Inode &in)
{
    disk.flush();
    disk.clear();

    if (in.isDirectory)
    {
        cerr << "recoverFile | Error: Cannot recover a directory as a file!" << endl;
        return vector<char>();
    }

    vector<char> recoveredFile(in.fileSize);
    int bytesToRead = in.fileSize;
    char *writePtr = recoveredFile.data();

    int indirectBlockLocator = 0;
    for (int i = 0; i < in.blockCount; i++)
    {
        int amountToRead = bytesToRead < sb.blockSize ? bytesToRead : sb.blockSize;
        long readSpot = 0;
        if (i >= Inode::MAX_DIRECT_BLOCKS)
        {
            long pos = sb.dataRegionStart + (in.indirectBlocks * sb.blockSize) + indirectBlockLocator * sizeof(int);
            int blockId;
            disk.seekg(pos, ios::beg);
            disk.read(reinterpret_cast<char *>(&blockId), sizeof(int));
            readSpot = sb.dataRegionStart + (blockId * sb.blockSize);
            indirectBlockLocator++;
        }
        else
        {
            readSpot = sb.dataRegionStart + (in.directBlocks[i] * sb.blockSize);
        }
        disk.seekg(readSpot, ios::beg);
        disk.read(writePtr, amountToRead);
        bytesToRead -= amountToRead;
        writePtr += amountToRead;
    }

    disk.flush();
    disk.clear();
    return recoveredFile;
}

// dirEntryIndex is the index of the DirectoryEntry (index inside a single block)
DirectoryEntry BlockStorageEngine::readDirectoryEntry(int dirEntryOffset, int blockIndex)
{
    disk.clear();
    DirectoryEntry myDirEntry;
    long pos = sb.dataRegionStart + (sb.blockSize * blockIndex) + (dirEntryOffset * sizeof(DirectoryEntry));
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myDirEntry), sizeof(myDirEntry));
    return myDirEntry;
}

// WRITE TO DISK
void BlockStorageEngine::writeDataToBlock(int blockID, char *dataPtr, int amountToWrite, int &bytesRemaining)
{
    long writeLocation = sb.dataRegionStart + (blockID * sb.blockSize);
    disk.seekp(writeLocation, ios::beg);
    disk.write(dataPtr, amountToWrite);
    bytesRemaining -= amountToWrite;
    sb.freeBlockCount--;
}

void BlockStorageEngine::writeInode(Inode &in, int inodeIndex)
{
    long pos = sb.inodeTableStart + (inodeIndex * sizeof(Inode));
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char *>(&in), sizeof(Inode));
    disk.flush();
}

void BlockStorageEngine::writeDirEntry(DirectoryEntry &ent, int dirEntryOffset, int blockIndex)
{
    disk.clear();
    long pos = sb.dataRegionStart + (blockIndex * sb.blockSize) + dirEntryOffset * sizeof(DirectoryEntry);
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char *>(&ent), sizeof(DirectoryEntry));
    disk.flush();
}

// writes a new directory entry into the parent directory's block
void BlockStorageEngine::addDirectoryEntry(const char *fileName, int dirInodeIndex, int targetInodeIndex)
{
    disk.flush();
    disk.clear();
    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "addDirectoryEntry | Error: Inode at index " << dirInodeIndex << " is not a directory!" << endl;
        return;
    }

    if (findInDirectory(fileName, dirInodeIndex) != -1)
    {
        cerr << "addDirectoryEntry | Error: File with name '" << fileName << "' already exists in directory with inode index " << dirInodeIndex << "!" << endl;
        return;
    }

    if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
    {
        cout << "addDirectoryEntry | Error: Directory has reached maximum entry limits!" << endl;
        return;
    }

    DirectoryEntry newEntry;
    strncpy(newEntry.fileName, fileName, sizeof(newEntry.fileName) - 1);
    newEntry.fileName[sizeof(newEntry.fileName) - 1] = '\0'; // Ensure null-termination
    newEntry.inodeIndex = targetInodeIndex;
    newEntry.isAllocated = true;

    // find a free directory entry
    int dirEntryIndex = findFreeDirEntry(in.directBlocks, in.blockCount);
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);

    int dirEntBlock = 0;
    int dirEntOffset = 0;

    long byteOffset; // write position

    // if the directory entry is already allocated, find the block and offset
    if (dirEntryIndex != -1)
    {
        dirEntBlock = dirEntryIndex / dirEntryPerBlock;
        dirEntOffset = dirEntryIndex % dirEntryPerBlock;

        byteOffset = sb.dataRegionStart + (in.directBlocks[dirEntBlock] * sb.blockSize) + (dirEntOffset * sizeof(DirectoryEntry));
    }
    else
    {
        // if the last block is full or no blocks are allocated yet allocate a new block
        if (in.blockCount == 0 || in.lastBlockUsedBytes == sb.blockSize)
        {
            int blockId = findFreeBlock();
            if (blockId == -1)
            {
                cerr << "addDirectoryEntry | Error: Disk is full, cannot allocate block for new directory entry!" << endl;
                return;
            }
            in.directBlocks[in.blockCount] = blockId;
            in.blockCount++;
            in.lastBlockUsedBytes = 0;
            byteOffset = sb.dataRegionStart + (blockId * sb.blockSize);
            sb.freeBlockCount--;
        }
        else
        {
            dirEntBlock = in.directBlocks[in.blockCount - 1];
            byteOffset = sb.dataRegionStart + (dirEntBlock * sb.blockSize) + in.lastBlockUsedBytes;
        }
    }
    disk.seekp(byteOffset, ios::beg);
    disk.write(reinterpret_cast<char *>(&newEntry), sizeof(newEntry));
    disk.flush();

    in.lastBlockUsedBytes += sizeof(newEntry);
    in.referenceCount++;
    writeInode(in, dirInodeIndex);
    syncSuperBlock();
}

// VALIDATION
void BlockStorageEngine::preSaveCheck(long dataSize) // In Bytes
{
    // checking for total blocks needed
    long blockNeeded = (dataSize / sb.blockSize);
    blockNeeded += (dataSize % sb.blockSize == 0) ? 0 : 1;
    if (blockNeeded > sb.freeBlockCount)
    {
        allocError = AllocError::DISK_FULL;
        return;
    }

    // checking for MAX_FILE_SIZE
    const int PTR_SIZE = sizeof(int);
    const int PTRS_PER_BLOCK = sb.blockSize / PTR_SIZE;
    const long MAX_SIZE = (Inode::MAX_DIRECT_BLOCKS * sb.blockSize) + (sb.blockSize * PTRS_PER_BLOCK);
    if (dataSize > MAX_SIZE)
    {
        allocError = AllocError::EXCEEDS_MAX_FILE_SIZE;
        return;
    }
}

// SEARCH AND TRAVERSAL
// searches for a file inside a directory, returns the inode index of the file, -1 if not found
int BlockStorageEngine::findInDirectory(const char *entityName, int dirInodeIndex)
{
    disk.clear();

    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "findInDirectory | Error: Inode at index " << dirInodeIndex << " is not a directory!" << endl;
        return -1;
    }
    int lastUsedBlock = in.blockCount - 1;
    if (lastUsedBlock == -1)
    {
        return -1; // Directory is empty, so file cannot be found
    }
    for (int i = 0; i <= lastUsedBlock; i++)
    {
        int blockSize = sb.blockSize;
        if (i == lastUsedBlock && in.lastBlockUsedBytes > 0)
        {
            blockSize = in.lastBlockUsedBytes;
        }
        for (int j = 0; j < blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry entry;
            disk.seekg(sb.dataRegionStart + (in.directBlocks[i] * sb.blockSize) + (j * sizeof(DirectoryEntry)), ios::beg);
            disk.read(reinterpret_cast<char *>(&entry), sizeof(DirectoryEntry));
            if (strncmp(entry.fileName, entityName, DirectoryEntry::MAX_FILE_NAME_LENGTH) == 0)
            {
                disk.clear();
                return entry.inodeIndex; // Found the file, return its inode index
            }
        }
    }
    return -1; // File not found in the directory
}

// traverses a directory path and returns the Inode Index of the final dir
int BlockStorageEngine::traversePath(vector<string> path)
{
    disk.clear();
    int curInodeIndex = 0;
    for (int i = 0; i < path.size(); i++)
    {
        Inode in = readInode(curInodeIndex);
        if (!in.isDirectory)
            return -1;
        for (int j = 0; j < in.blockCount; j++)
        {
            int blockIndex = in.directBlocks[j];
            bool found = false;
            for (int k = 0; k < sb.blockSize / sizeof(DirectoryEntry); k++)
            {
                DirectoryEntry ent = readDirectoryEntry(k, blockIndex);
                if (strncmp(ent.fileName, path[i].c_str(), sizeof(ent.fileName)) == 0)
                {
                    curInodeIndex = ent.inodeIndex;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
            if (!found && j == in.blockCount - 1)
                return -1;
        }
    }
    return curInodeIndex;
}

// returns the index of the DirectoryEntry (index starting from the first block)
int BlockStorageEngine::findDirEntry(int dirInodeIndex, const char *fileName)
{
    int dirEntryCounter = -1;
    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cout << "findDirEntry | Inode isn't a directory" << endl;
        return -1;
    }
    for (int i = 0; i < in.blockCount; i++)
    {
        for (int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            dirEntryCounter++;
            DirectoryEntry ent = readDirectoryEntry(j, in.directBlocks[i]);
            if (strncmp(ent.fileName, fileName, DirectoryEntry::MAX_FILE_NAME_LENGTH) == 0)
            {
                return dirEntryCounter;
            }
        }
    }
    return -1;
}

// PUBLIC METHODS

void BlockStorageEngine::printBitMap()
{
    disk.flush();
    disk.clear();

    cout << "Bitmap Status: " << endl;
    for (int i = 0; i < sb.blockCount; i++)
    {
        cout << (isBlockFree(i) ? "0" : "1") << ' ';
        if ((i + 1) % 64 == 0)
        {
            cout << endl; // New line after every 64 blocks for better readability
        }
    }
}

// CREATE / INITIATE / DISCONTINUE A DISK INSTANCE
void BlockStorageEngine::createDisk(const string &name, long sizeInMB)
{
    string baseDir = getAppDirectory();
    fs::create_directories(baseDir);

    string fullPath = baseDir + name + SuperBlock::DISK_EXTENSION;
    if (findFileInDirectory(name, baseDir))
    {
        cerr << "createDisk | This disk already exists! " << endl;
        return;
    }
    {
        ofstream create(fullPath, ios::binary);
    }
    disk.open(fullPath, ios::in | ios::out | ios::binary | ios::trunc);

    if (!disk.is_open())
    {
        allocError = AllocError::CANNOT_CREATE_FILE;
        cerr << "createDisk | Error: Could not create disk file at " << fullPath << endl;
        return;
    }

    cout << "Disk created at: " << fullPath << endl;
    size_t totalBytes = (sizeInMB * 1024 * 1024);
    size_t inodeRegionSize = totalBytes / 20; // 5% of the disk size is reserved for inodes
    size_t superBlockSize = sizeof(SuperBlock);
    size_t blockCount = (totalBytes - superBlockSize - inodeRegionSize) / sb.blockSize;
    size_t blockBitmapSize = (blockCount + 7) / 8; // (a + b -1) / b = ceil(a/b)
    size_t usableBytes = totalBytes - superBlockSize - inodeRegionSize - blockBitmapSize;

    sb.blockCount = usableBytes / sb.blockSize;
    sb.inodeCount = inodeRegionSize / sizeof(Inode);
    updateSbInfo();
    int size = calculateTotalSize();
    formatDisk(size);

    Inode root;
    root.isDirectory = true;
    root.fileSize = 0;
    root.isAllocated = true;
    writeInode(root, 0);
    sb.allocatedInodeCount++;
    syncSuperBlock();
    disk.flush();
}

void BlockStorageEngine::mountDisk(const string &name)
{
    string baseDir = getAppDirectory();
    if (!findFileInDirectory(name, baseDir))
    {
        cerr << "mountDisk | This disk haven't been created! " << endl;
    }

    string diskPath = baseDir + name + SuperBlock::DISK_EXTENSION;
    disk.open(diskPath, ios::in | ios::out | ios::binary);

    long diskSize = 0;
    disk.seekg(0, ios::end);
    diskSize = disk.tellg();
    disk.seekg(0, ios::beg);
    sb.blockCount = diskSize / sb.blockSize;

    SuperBlock tempSb;
    disk.read(reinterpret_cast<char *>(&tempSb), sizeof(SuperBlock));
    if (tempSb.magicNumber != SuperBlock::MAGIC_NUMBER)
    {
        cerr << "mountDisk | Error: Disk file is corrupted or not a valid Block Storage Engine disk!" << endl;
        disk.close();
        return;
    }
    sb = tempSb;
}

void BlockStorageEngine::unmountDisk()
{
    updateSbInfo();
    syncSuperBlock();
    if (disk.is_open())
    {
        disk.close();
    }
}

// CREATION OR DELETION OF FILES AND DIR'S
void BlockStorageEngine::save(const string &fileName, const string &filePath, int inodeIndex)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "save | Error: Invalid Path" << endl;
        return;
    }
    string rfile = parsedPath[parsedPath.size() - 1];

    if (findInDirectory(rfile.c_str(), dirInodeIndex) != -1)
    {
        cerr << "save | Error: File with name '" << rfile << "' already exists in directory with inode index " << dirInodeIndex << "!" << endl;
        return;
    }

    // open the file to wanted to save
    fstream fileToSave;
    fileToSave.open(filePath, ios::in | ios::out | ios::binary);
    if (!fileToSave.is_open())
    {
        cerr << "save | Error: Could not open file at " << filePath << endl;
        return;
    }

    // find the file size
    long fileSize = 0;
    fileToSave.seekg(0, ios::end);
    fileSize = fileToSave.tellg();
    fileToSave.seekg(0, ios::beg);

    // create a buffer and read the file data to the buffer
    vector<char> buffer(fileSize);
    char *bufferPtr = buffer.data();
    fileToSave.read(bufferPtr, fileSize);

    fileToSave.close();

    // make a check if the fileSize fits to the maximum size allowed by the disk and if the disk have enough free space
    preSaveCheck(fileSize);
    if (allocError != AllocError::OK)
    {
        if (allocError == AllocError::DISK_FULL)
        {
            cerr << "save | Error: Not enough free space on disk to save file!" << endl;
        }
        else if (allocError == AllocError::EXCEEDS_MAX_FILE_SIZE)
        {
            cerr << "save | Error: File size exceeds maximum allowed size of " << (Inode::MAX_DIRECT_BLOCKS * sb.blockSize) << " bytes!" << endl;
        }
        return;
    }

    // create a file Inode
    Inode fileInode;
    fileInode.fileSize = buffer.size();
    fileInode.isAllocated = true;
    fileInode.referenceCount = 1;

    // find and allocate free blocks for the fileInode based on the it's file size
    allocateBlock(fileInode, buffer);

    // update info saved in the disk
    syncSuperBlock();
    int freeInodeIndex = inodeIndex;
    if (inodeIndex == -1)
    {
        freeInodeIndex = findFreeInode();
    }
    if (freeInodeIndex == -1)
    {
        cerr << "save | Error: Disk is full, cannot save file!" << endl;
        return;
    }
    writeInode(fileInode, freeInodeIndex);
    sb.allocatedInodeCount++;

    // add the file entry to it's parent folder
    addDirectoryEntry(rfile.c_str(), dirInodeIndex, freeInodeIndex); // Add entry to root directory
    cout << "File '" << rfile << "' saved successfully!" << endl;
}

void BlockStorageEngine::createDirectory(const string &path)
{
    disk.clear();
    vector<string> parsedPath = parseString(path, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "createDirectory | Error: Invalid Path" << endl;
        return;
    }
    string lastElem = parsedPath[parsedPath.size() - 1];
    // cout << "Last Element: " << lastElem << endl;
    // cout << "Parent Path: " << joinString(parentPath, '/') << endl;
    // cout << "Dir Inode Index: " << dirInodeIndex << endl;

    if (findInDirectory(lastElem.c_str(), dirInodeIndex) != -1)
    {
        cerr << "createDirectory | Error: Directory with name '" << lastElem << "' already exists in directory with inode index " << dirInodeIndex << "!" << endl;
        return;
    }

    Inode dir;
    dir.isDirectory = true;
    dir.isAllocated = true;
    int freeInodeIndex = findFreeInode();
    // cout << "Free Inode Index: " << freeInodeIndex << endl;
    writeInode(dir, freeInodeIndex);
    addDirectoryEntry(lastElem.c_str(), dirInodeIndex, freeInodeIndex);
    sb.allocatedInodeCount++;
    disk.flush();
    cout << "Directory created at: " << path << endl;
}

void BlockStorageEngine::remove(const string &fileName)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "remove | Error: Invalid Path" << endl;
        return;
    }
    string file = parsedPath[parsedPath.size() - 1];

    int inodeIndex = findInDirectory(file.c_str(), dirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "remove | Error: File not found!" << endl;
        return;
    }

    int dirEntryIndex = findDirEntry(dirInodeIndex, file.c_str());
    if (dirEntryIndex == -1)
    {
        cerr << "remove | Error: File link not found!" << endl;
        return;
    }

    freeDirectoryEntry(dirEntryIndex, dirInodeIndex);

    Inode in = readInode(inodeIndex);
    in.referenceCount--;
    if (in.referenceCount == 0)
    {
        for (int i = 0; i < in.blockCount; i++)
        {
            if (i < Inode::MAX_DIRECT_BLOCKS)
            {
                freeBlock(in.directBlocks[i]);
                in.directBlocks[i] = -1;
            }
        }
        if (in.indirectBlocks != -1)
        {
            int usedCount = in.blockCount - Inode::MAX_DIRECT_BLOCKS;
            freeIndirectBlocks(in.indirectBlocks, usedCount);
            freeBlock(in.indirectBlocks);
            in.indirectBlocks = -1;
        }
        in.blockCount = 0;
        in.indirectBlocks = -1;
        in.isAllocated = false;
        in.fileSize = 0;
        in.isDirectory = false;
    }
    writeInode(in, inodeIndex);
}


void BlockStorageEngine::removeDirectory(const string &path)
{
    vector<string> parsedPath = parseString(path, '/');
    int dirInodeIndex = traversePath(parsedPath);
    if (dirInodeIndex == -1)
    {
        cerr << "removeDirectory | Error: Invalid Path" << endl;
        return;
    }
    string lastElem = parsedPath[parsedPath.size() - 1];
   
    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "removeDirectory | Error: Path is not a directory!" << endl;
        return;
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        for (int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent = readDirectoryEntry(j, in.directBlocks[i]);
            if (ent.isAllocated)
            {
                string entPathStr = path + "/" + ent.fileName;
                Inode entInode = readInode(ent.inodeIndex);

                if (entInode.isDirectory)
                    removeDirectory(entPathStr);
                else
                    remove(entPathStr);
                ent.isAllocated = false;
            }
        }
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        freeBlock(in.directBlocks[i]);
        in.directBlocks[i] = -1;
    }

    in.lastBlockUsedBytes = 0;
    in.blockCount = 0;
    in.isAllocated = false;
    in.fileSize = 0;
    in.isDirectory = false;
    writeInode(in, dirInodeIndex);

    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int parentInodeIndex = traversePath(parentPath);
    if (parentInodeIndex == -1)
    {
        cerr << "removeDirectory | Error: Parent Directory Invalid Path" << endl;
        return;
    }

    int dirEntryIndex = findDirEntry(parentInodeIndex, lastElem.c_str());
    freeDirectoryEntry(dirEntryIndex, parentInodeIndex);
}

// RETRIEVAL OF FILES
void BlockStorageEngine::retrieve(const string &fileName, const string &destPath)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "retrieve | Error: Invalid Path" << endl;
        return;
    }
    string rfile = parsedPath[parsedPath.size() - 1];

    int inodeIndex = findInDirectory(rfile.c_str(), dirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "retrieve | Error: File '" << fileName << "' not found in directory index: " << dirInodeIndex << "!" << endl;
        return;
    }
    Inode recoveryInode = readInode(inodeIndex);

    vector<char> fileData = recoverFile(recoveryInode);
    fs::path outPath = fs::path(destPath) / rfile;
    ofstream output(outPath, ios::binary);
    if (!output.is_open())
    {
        cerr << "retrieve | Error: Could not create output file at " << outPath << endl;
        return;
    }
    output.write(fileData.data(), fileData.size());
    output.close();
}

// AUXILIARY FUNCTIONS

void BlockStorageEngine::link(const string &nfile, const string &efile)
{
    disk.clear();
    // efile: existing file, nfile: new file name
    vector<string> ePath = parseString(efile, '/');
    vector<string> e_parentPath(ePath.begin(), ePath.end() - 1);
    int e_dirInodeIndex = traversePath(e_parentPath);
    if (e_dirInodeIndex == -1)
    {
        cout << "link | The specified directory doesn't exist!" << endl;
        return;
    }
    string efileName = ePath[ePath.size() - 1];
    int e_inodeIndex = findInDirectory(efileName.c_str(), e_dirInodeIndex);
    if (e_inodeIndex == -1)
    {
        cout << "link | The file doesn't exist in the specified directory!" << endl;
        return;
    }

    Inode e_inode = readInode(e_inodeIndex);
    e_inode.referenceCount++;
    writeInode(e_inode, e_inodeIndex);

    vector<string> nPath = parseString(nfile, '/');
    vector<string> n_parentPath(nPath.begin(), nPath.end() - 1);
    int n_dirInodeIndex = traversePath(n_parentPath);
    if (n_dirInodeIndex == -1)
    {
        cout << "link | The specified directory for the new file doesn't exist!" << endl;
        return;
    }
    string nfileName = nPath[nPath.size() - 1];
    addDirectoryEntry(nfileName.c_str(), n_dirInodeIndex, e_inodeIndex);
    cout << "Link created between '" << efile << "' and '" << nfile << "'!" << endl;
}

void BlockStorageEngine::list(string path)
{
    vector<string> parsedPath = parseString(path, '/');
    int dirInodeIndex = traversePath(parsedPath);
    if (dirInodeIndex == -1)
    {
        cerr << "list | Error: Invalid Path" << endl;
        return;
    }

    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "list | Error: Path is not a directory!" << endl;
        return;
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        for (int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent = readDirectoryEntry(j, in.directBlocks[i]);
            if (ent.isAllocated)
            {
                cout << ent.fileName << endl;
            }
        }
    }
}

void BlockStorageEngine::move(const string &file, const string &dPath)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "move | Error: Invalid file Path" << endl;
        return;
    }
    string fileName = parsedPath[parsedPath.size() - 1];

    int dirEntryIndex = findDirEntry(dirInodeIndex, fileName.c_str());
    if (dirEntryIndex == -1)
    {
        cerr << "move | Error: DirEntry of file not found! file: " << fileName << endl;
        return;
    }
    int inodeIndex = findInDirectory(fileName.c_str(), dirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "move | Error: File not found in directory index: " << dirInodeIndex << "!" << endl;
        return;
    }

    vector<string> dPathParsed = parseString(dPath, '/');
    vector<string> dParentPath(dPathParsed.begin(), dPathParsed.end());
    int dDirInodeIndex = traversePath(dParentPath);
    if (dDirInodeIndex == -1)
    {
        cerr << "move | Error: Invalid Path" << endl;
        return;
    }
    Inode dInode = readInode(dDirInodeIndex);
    if (!dInode.isDirectory)
    {
        cerr << "move | Error: Path is not a directory!" << endl;
        return;
    }

    addDirectoryEntry(fileName.c_str(), dDirInodeIndex, inodeIndex);
    freeDirectoryEntry(dirEntryIndex, dirInodeIndex);
    cout << "File moved from '" << file << "' to '" << dPath << "'!" << endl;
}

void BlockStorageEngine::rename(const string &file, const string &nName)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "rename | Error: Invalid Path" << endl;
        return;
    }
    string fileName = parsedPath[parsedPath.size() - 1];

    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "rename | Error: Path is not a directory!" << endl;
        return;
    }

    int dirEntryIndex = findDirEntry(dirInodeIndex, fileName.c_str());
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;
    int dirEntBlock = dirEntryIndex / dirEntryPerBlock;
    int blockIndex = in.directBlocks[dirEntBlock];

    DirectoryEntry ent = readDirectoryEntry(dirEntOffset, blockIndex);
    strncpy(ent.fileName, nName.c_str(), DirectoryEntry::MAX_FILE_NAME_LENGTH - 1);
    ent.isAllocated = true;
    ent.fileName[DirectoryEntry::MAX_FILE_NAME_LENGTH - 1] = '\0';
    writeDirEntry(ent, dirEntOffset, blockIndex);
    cout << "File renamed from '" << file << "' to '" << nName << "'!" << endl;
}

void BlockStorageEngine::replace(const string &file, const string &newFilePath)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = traversePath(parentPath);
    if (dirInodeIndex == -1)
    {
        cerr << "replace | Error: Invalid Path" << endl;
        return;
    }
    string fileName = parsedPath[parsedPath.size() - 1];
    int inodeIndex = findInDirectory(fileName.c_str(), dirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "replace | Error: File not found!" << endl;
        return;
    }

    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "replace | Error: Path is not a directory!" << endl;
        return;
    }
    int dirEntryIndex = findDirEntry(dirInodeIndex, fileName.c_str());
    if (dirEntryIndex == -1)
    {
        cerr << "replace | Error: DirEntry of file not found! file: " << fileName << endl;
        return;
    }
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;
    int dirEntBlock = dirEntryIndex / dirEntryPerBlock;
    int blockIndex = in.directBlocks[dirEntBlock];

    vector<string> newPathParsed = parseString(newFilePath, '/');
    string newFileName = newPathParsed[newPathParsed.size() - 1];

    // save resets reference count to 0 so we have to save before and restore after save
    Inode oldIn = readInode(inodeIndex);
    int oldRefCount = oldIn.referenceCount;
    remove(file);
    parentPath.push_back(newFileName);
    string destPath = joinString(parentPath, '/');
    save(destPath, newFilePath, inodeIndex);

    // restore reference count
    Inode newIn = readInode(inodeIndex);
    newIn.referenceCount = oldRefCount;
    newIn.isAllocated = true;
    writeInode(newIn, inodeIndex);
    cout << "File replaced with '" << newFilePath << "'!" << endl;
}

void BlockStorageEngine::diskInfo()
{
    cout << "Disk Info:" << endl;
    cout << "Disk Size: " << calculateTotalSize() << " bytes | " << calculateTotalSize() / 1024 / 1024 << " MB" << endl;
    cout << "Block Size: " << sb.blockSize << " bytes" << endl;
    cout << "Block Count: " << sb.blockCount << endl;
    cout << "Inode Count: " << sb.inodeCount << endl;
    cout << "Allocated Inode Count: " << sb.allocatedInodeCount << endl;
    cout << "Free Block Count: " << sb.freeBlockCount << endl;
    cout << "Bitmap Size: " << sb.bitmapSize << endl;
    cout << "Bitmap Start: " << sb.bitmapStart << endl;
}

void BlockStorageEngine::printFileStructure(int dirInodeIndex, int depth)
{
    Inode in = readInode(dirInodeIndex);

    for (int j = 0; j < in.blockCount; j++)
    {
        for (int k = 0; k < sb.blockSize / sizeof(DirectoryEntry); k++)
        {
            DirectoryEntry ent = readDirectoryEntry(k, in.directBlocks[j]);
            if (!ent.isAllocated)
                continue;
            Inode in = readInode(ent.inodeIndex);
            if (!in.isDirectory)
            {
                for (int i = 0; i < depth; i++)
                    cout << "  ";
                cout << ent.fileName << endl;
            }
            else
            {
                for (int i = 0; i < depth; i++)
                    cout << "  ";
                cout << ent.fileName << "/" << endl;
                printFileStructure(ent.inodeIndex, depth + 1);
            }
        }
    }
}
