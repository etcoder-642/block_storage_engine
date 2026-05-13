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
    for (long i = 0; i < size; i++)
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

void BlockStorageEngine::freeBlock(int index)
{
    int pos = sb.dataRegionStart + (index * sb.blockSize);
    formatDisk(sb.blockSize, pos);
    setBitFree(index);
    sb.freeBlockCount--;
}

void BlockStorageEngine::freeIndirectBlocks(int index)
{
    int pos = sb.dataRegionStart + (index * sb.blockSize);
    int intsInBlock = sb.blockSize / sizeof(int);
    int blockIds[intsInBlock];
    disk.seekg(sb.dataRegionStart + (index * sb.blockSize), ios::beg);
    disk.read(reinterpret_cast<char *>(&blockIds), sb.blockSize);
    for (int i = 0; i < intsInBlock; i++)
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
        cerr << "Error: Cannot allocate blocks for a directory!" << endl;
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
            cerr << "Error: Disk is full, cannot allocate more blocks!" << endl;
            return;
        }

        if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
        {
            in.indirectBlocks = freeBlockID;
            sb.freeBlockCount--;
            this->allocateIndirectBlock(in, data, bytesRemaining);
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
            cerr << "Error: Disk is full, cannot allocate more blocks!" << endl;
            return;
        }

        int blocksInTheIndirectBlock = in.blockCount - Inode::MAX_DIRECT_BLOCKS;
        if (blocksInTheIndirectBlock >= sb.blockSize / sizeof(int))
        {
            cout << "Error: indirect block full, file too large!" << endl;
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
        cerr << "Error: Cannot recover a directory as a file!" << endl;
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
DirectoryEntry BlockStorageEngine::readDirectoryEntry(int dirEntryIndex, int blockIndex)
{
    disk.clear();
    DirectoryEntry myDirEntry;
    long pos = sb.dataRegionStart + (sb.blockSize * blockIndex) + (dirEntryIndex * sizeof(DirectoryEntry));
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

void BlockStorageEngine::writeDirEntry(DirectoryEntry &ent, int dirEntryIndex, int blockIndex)
{
    disk.clear();
    long pos = sb.dataRegionStart + (blockIndex * sb.blockSize) + dirEntryIndex * sizeof(DirectoryEntry);
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
        cerr << "Error: Inode at index " << dirInodeIndex << " is not a directory!" << endl;
        return;
    }

    if (findInDirectory(fileName, dirInodeIndex) != -1)
    {
        cerr << "Error: File with name '" << fileName << "' already exists in directory with inode index " << dirInodeIndex << "!" << endl;
        return;
    }

    if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
    {
        cout << "Error: Directory has reached maximum entry limits!" << endl;
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
        dirEntBlock = (dirEntryIndex + dirEntryPerBlock - 1) / dirEntryPerBlock; // (a + b -1) / b = ceil(a/b)
        dirEntOffset = dirEntryIndex % dirEntryPerBlock;

        byteOffset = sb.dataRegionStart + (dirEntBlock * sb.blockSize) + dirEntOffset * sizeof(DirectoryEntry);
    }
    else
    {
        // if the last block is full or no blocks are allocated yet allocate a new block
        if (in.blockCount == 0 || in.lastBlockUsedBytes == sb.blockSize)
        {
            int blockId = findFreeBlock();
            if (blockId == -1)
            {
                cerr << "Error: Disk is full, cannot allocate block for new directory entry!" << endl;
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
int BlockStorageEngine::findInDirectory(const char *fileName, int dirInodeIndex)
{
    disk.clear();

    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "Error: Inode at index " << dirInodeIndex << " is not a directory!" << endl;
        return -1;
    }
    int lastUsedBlock = in.blockCount - 1;
    if (lastUsedBlock == -1)
    {
        return -1; // Directory is empty, so file cannot be found
    }
    for (int i = 0; i <= lastUsedBlock; i++)
    {
        int blockSize = (i != lastUsedBlock) ? sb.blockSize : in.lastBlockUsedBytes;
        for (int j = 0; j < blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry entry;
            disk.seekg(sb.dataRegionStart + (in.directBlocks[i] * sb.blockSize) + (j * sizeof(DirectoryEntry)), ios::beg);
            disk.read(reinterpret_cast<char *>(&entry), sizeof(DirectoryEntry));
            if (strncmp(entry.fileName, fileName, DirectoryEntry::MAX_FILE_NAME_LENGTH) == 0)
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
        Inode in = this->readInode(curInodeIndex);
        if (!in.isDirectory)
            return -1;
        for (int j = 0; j < in.blockCount; j++)
        {
            int blockIndex = in.directBlocks[j];
            bool found = false;
            for (int k = 0; k < sb.blockSize / sizeof(DirectoryEntry); k++)
            {
                DirectoryEntry ent = this->readDirectoryEntry(k, blockIndex);
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

// returns the index of the DirectoryEntry (index starts from the first block)
int BlockStorageEngine::findDirEntry(int inodeIndex, const char *fileName)
{
    int dirEntryCounter = -1;
    Inode in = readInode(inodeIndex);
    if (!in.isDirectory)
    {
        cout << "Inode isn't a directory" << endl;
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
    return dirEntryCounter;
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
        cerr << "This disk already exists! " << endl;
        return;
    }
    {
        ofstream create(fullPath, ios::binary);
    }
    disk.open(fullPath, ios::in | ios::out | ios::binary | ios::trunc);

    if (!disk.is_open())
    {
        allocError = AllocError::CANNOT_CREATE_FILE;
        cerr << "Error: Could not create disk file at " << fullPath << endl;
        return;
    }

    cout << "Disk created at: " << fullPath << endl;
    sb.blockCount = (sizeInMB * 1024 * 1024) / sb.blockSize;
    updateSbInfo();
    int size = calculateTotalSize();
    formatDisk(size);

    createDirectory("root");
    syncSuperBlock();
    disk.flush();
}

void BlockStorageEngine::mountDisk(const string &name)
{
    string baseDir = getAppDirectory();
    if (!findFileInDirectory(name, baseDir))
    {
        cerr << "This disk haven't been created! " << endl;
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
        cerr << "Error: Disk file is corrupted or not a valid Block Storage Engine disk!" << endl;
        disk.close();
        return;
    }
}

void BlockStorageEngine::unmountDisk()
{
    updateSbInfo();
    updateBitMap();
    syncSuperBlock();
    if (disk.is_open())
    {
        disk.close();
    }
}

// CREATION OR DELETION OF FILES AND DIR'S
void BlockStorageEngine::save(const string &fileName, const string &filePath)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int finalDirInodeIndex = traversePath(parentPath);
    if (finalDirInodeIndex == -1)
    {
        cerr << "Error: Invalid Path" << endl;
        return;
    }
    string rfile = parsedPath[parsedPath.size() - 1];

    // open the file to wanted to save
    fstream fileToSave;
    fileToSave.open(filePath, ios::in | ios::out | ios::binary);
    if (!fileToSave.is_open())
    {
        cerr << "Error: Could not open file at " << filePath << endl;
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
            cerr << "Error: Not enough free space on disk to save file!" << endl;
        }
        else if (allocError == AllocError::EXCEEDS_MAX_FILE_SIZE)
        {
            cerr << "Error: File size exceeds maximum allowed size of " << (Inode::MAX_DIRECT_BLOCKS * sb.blockSize) << " bytes!" << endl;
        }
        return;
    }

    // create a file Inode
    Inode fileInode;
    fileInode.fileSize = buffer.size();

    // find and allocate free blocks for the fileInode based on the it's file size
    allocateBlock(fileInode, buffer);

    // update info saved in the disk
    syncSuperBlock();
    int freeInodeIndex = findFreeInode();
    writeInode(fileInode, freeInodeIndex);
    sb.allocatedInodeCount++;

    // add the file entry to it's parent folder
    addDirectoryEntry(rfile.c_str(), finalDirInodeIndex, freeInodeIndex); // Add entry to root directory
    cout << "File '" << rfile << "' saved successfully!" << endl;
}

void BlockStorageEngine::createDirectory(const string &path)
{
    disk.clear();
    vector<string> parsedPath = parseString(path, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int finalDirInodeIndex = this->traversePath(parentPath);
    if (finalDirInodeIndex == -1)
    {
        cerr << "Error: Invalid Path" << endl;
        return;
    }
    string lastElem = parsedPath[parsedPath.size() - 1];

    Inode dir;
    dir.isDirectory = true;
    int freeInodeIndex = findFreeInode();
    this->writeInode(dir, freeInodeIndex);
    this->addDirectoryEntry(lastElem.c_str(), finalDirInodeIndex, sb.allocatedInodeCount - 1);
    sb.allocatedInodeCount++;
    disk.flush();
}

void BlockStorageEngine::remove(const string &fileName)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int finalDirInodeIndex = traversePath(parentPath);
    if (finalDirInodeIndex == -1)
    {
        cerr << "Error: Invalid Path" << endl;
        return;
    }
    string file = parsedPath[parsedPath.size() - 1];

    int inodeIndex = findInDirectory(file.c_str(), finalDirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "Error: File not found!" << endl;
        return;
    }

    int dirEntryIndex = findDirEntry(inodeIndex, file.c_str());
    if (dirEntryIndex == -1)
    {
        cerr << "Error: File link not found!" << endl;
        return;
    }

    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);

    int dirEntBlock = (dirEntryIndex + dirEntryPerBlock - 1) / dirEntryPerBlock; // (a + b -1) / b = ceil(a/b)
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;

    DirectoryEntry ent = readDirectoryEntry(dirEntOffset, dirEntBlock);
    ent.isAllocated = false;
    writeDirEntry(ent, dirEntOffset, dirEntBlock);

    Inode in = readInode(inodeIndex);
    in.referenceCount--;
    if (in.referenceCount == 0)
    {
        for (int i = 0; i < in.blockCount; i++)
        {
            freeBlock(in.directBlocks[i]);
            in.directBlocks[i] = -1;
        }
        in.blockCount = 0;
        freeIndirectBlocks(in.indirectBlocks);
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
        cerr << "Error: Invalid Path" << endl;
        return;
    }
    Inode in = readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "Error: Path is not a directory!" << endl;
        return;
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        for (int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent = readDirectoryEntry(j, in.directBlocks[i]);
            if (ent.isAllocated)
            {
                vector<string> entPath = {path, ent.fileName};
                string entPathStr = joinString(entPath, '/');
                Inode entInode = readInode(ent.inodeIndex);

                if (entInode.isDirectory)
                    removeDirectory(entPathStr);
                else
                    remove(entPathStr);
                ent.isAllocated = false;
            }
        }
        freeBlock(in.directBlocks[i]);
        in.directBlocks[i] = -1;
    }
    in.blockCount = 0;
    in.isAllocated = false;
    in.fileSize = 0;
    in.isDirectory = false;
    writeInode(in, dirInodeIndex);
}

// RETRIEVAL OF FILES
void BlockStorageEngine::retrieve(const string &fileName, const string &destPath)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int finalDirInodeIndex = traversePath(parentPath);
    if (finalDirInodeIndex == -1)
    {
        cerr << "Error: Invalid Path" << endl;
        return;
    }
    string rfile = parsedPath[parsedPath.size() - 1];

    int inodeIndex = findInDirectory(rfile.c_str(), finalDirInodeIndex);
    if (inodeIndex == -1)
    {
        cerr << "Error: File '" << fileName << "' not found in directory index: " << finalDirInodeIndex << "!" << endl;
        return;
    }
    Inode recoveryInode = readInode(inodeIndex);

    vector<char> fileData = recoverFile(recoveryInode);
    fs::path outPath = fs::path(destPath) / fileName;
    ofstream output(outPath, ios::binary);
    if (!output.is_open())
    {
        cerr << "Error: Could not create output file at " << outPath << endl;
        return;
    }
    output.write(fileData.data(), fileData.size());
    output.close();
}

// AUXILIARY FUNCTIONS

void BlockStorageEngine::link(const char *nfile, const char *efile)
{
    disk.clear();
    // efile: existing file, nfile: new file name
    vector<string> ePath = parseString(efile, '/');
    vector<string> e_parentPath(ePath.begin(), ePath.end() - 1);
    int e_dirInodeIndex = traversePath(e_parentPath);
    if (e_dirInodeIndex == -1)
    {
        cout << "The specified directory doesn't exist!" << endl;
        return;
    }
    string efileName = ePath[ePath.size() - 1];
    int e_inodeIndex = findInDirectory(efileName.c_str(), e_dirInodeIndex);
    if (e_inodeIndex == -1)
    {
        cout << "The file doesn't exist in the specified directory!" << endl;
        return;
    }

    vector<string> nPath = parseString(nfile, '/');
    vector<string> n_parentPath(nPath.begin(), nPath.end() - 1);
    int n_dirInodeIndex = traversePath(n_parentPath);
    if (n_dirInodeIndex == -1)
    {
        cout << "The specified directory for the new file doesn't exist!" << endl;
        return;
    }
    string nfileName = ePath[ePath.size() - 1];
    addDirectoryEntry(nfileName.c_str(), n_dirInodeIndex, e_inodeIndex);
}
