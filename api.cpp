#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <cstring>
namespace fs = std::filesystem;

#include "api.h"

using namespace std;

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

bool findFileInDirectory(const string &fileName, const string &dirPath)
{
    fs::path needle = fs::path(dirPath) / (fileName + SuperBlock::DISK_EXTENSION);
    return fs::exists(needle);
}

// Could not open file  .bin Error

bool BlockStorageEngine::isBlockFree(int index)
{
    disk.clear();
    int maskingIndex = index % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    unsigned char mask = 1 << maskingIndex;
    disk.seekg(sb.bitmapStart + (index / 8), ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    disk.seekg(0, ios::beg); // Reset position after reading
    if (bitnum & mask)
    {
        return false;
    }
    else
        return true;
}

void BlockStorageEngine::setBlockOccupied(int index)
{
    disk.flush();
    disk.clear();
    int maskingIndex = index % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    short mask = 1 << maskingIndex;
    disk.seekg(sb.bitmapStart + (index / 8), ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    bitnum |= mask;
    disk.seekp(sb.bitmapStart + (index / 8), ios::beg);
    disk.write(reinterpret_cast<char *>(&bitnum), sizeof(unsigned char));
    disk.flush(); // Ensure the updated bitmap is written to disk
}
// calculates the total size of the drive
long BlockStorageEngine::calculateTotalSize()
{
    long totalSize = sizeof(SuperBlock) + sb.blockSize * sb.blockCount; // Size of all blocks
    totalSize += sb.bitmapSize;                                         // Add bitmap size
    totalSize += sb.inodeSize * sb.inodeCount;                          // Add size of all inodes
    return totalSize;
}

int BlockStorageEngine::findFreeBlock()
{
    int index = 0;

    // "Skip" all blocks that are already taken
    while (!isBlockFree(index))
    {
        index++;
        if (index >= sb.blockCount)
        {
            return -1; // Standard way to signal "Disk Full"
        }
    }

    // Now index is pointing to a free block!
    setBlockOccupied(index);
    return index;
}

void BlockStorageEngine::writeDataToBlock(int blockID, char *dataPtr, int amountToWrite, int &bytesRemaining)
{
    long writeLocation = sb.dataRegionStart + (blockID * sb.blockSize);
    disk.seekp(writeLocation, ios::beg);
    disk.write(dataPtr, amountToWrite);
    bytesRemaining -= amountToWrite;
    sb.freeBlockCount--;
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

void BlockStorageEngine::writeInode(Inode &in, int inodeIndex)
{
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char *>(&in), sb.inodeSize);
    disk.flush();
}

Inode BlockStorageEngine::readInode(int inodeIndex)
{
    Inode myInode;
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myInode), sb.inodeSize);
    disk.clear();
    return myInode;
}

/* A function to read a directoryEntry
Arguments:
   - blockIndex = on which index is the dirEntry located, expects the blockIndex of where the dirEntry is located
   - dirEntryIndex = the location of the dirEntry in the specified block (only can have values 0-63 cause a block can have 64 dirEntries)
*/

DirectoryEntry BlockStorageEngine::readDirectoryEntry(int dirEntryIndex, int blockIndex)
{
    disk.clear();
    DirectoryEntry myDirEntry;
    long pos = sb.dataRegionStart + (sb.blockSize * blockIndex) + (dirEntryIndex * sizeof(DirectoryEntry));
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myDirEntry), sizeof(myDirEntry));
    return myDirEntry;
}

void BlockStorageEngine::updateSuperBlock()
{
    disk.seekp(0, ios::beg);
    disk.write(reinterpret_cast<char *>(&sb), sizeof(SuperBlock));
    disk.flush();
}

void BlockStorageEngine::updateSbInfo()
{
    sb.bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
    sb.bitmapSize = sb.blockCount / 8;
    sb.inodeTableStart = sb.bitmapStart + sb.bitmapSize;
    sb.dataRegionStart = sb.inodeTableStart + (sb.inodeSize * sb.inodeCount);
    sb.freeBlockCount = sb.blockCount;

    sb.inodeTableStart = sb.bitmapStart + sb.bitmapSize;
    sb.dataRegionStart = sb.inodeTableStart + (sb.inodeSize * sb.inodeCount);
}

void BlockStorageEngine::formatDisk()
{
    disk.seekp(0, ios::beg);
    char zero = 0;
    for (long i = 0; i < calculateTotalSize(); i++)
    {
        disk.write(&zero, 1);
    }
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
            long pos = sb.dataRegionStart + (in.indirectBlocks * sb.blockSize) + indirectBlockLocator*sizeof(int);
            int blockId;
            disk.seekg(pos, ios::beg);
            disk.read(reinterpret_cast<char*>(&blockId), sizeof(int));
            readSpot = sb.dataRegionStart + (blockId  * sb.blockSize);
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
    this->updateSbInfo();
    this->formatDisk();

    Inode rootDir;
    rootDir.isDirectory = true;
    sb.allocatedInodeCount++;     // Account for root directory inode
    this->writeInode(rootDir, 0); // Write root directory inode at index 0
    this->updateSuperBlock();
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
    this->updateSbInfo();
}

void BlockStorageEngine::unmountDisk()
{
    if (disk.is_open())
    {
        disk.close();
    }
}

int BlockStorageEngine::findInDirectory(const char *fileName, int dirInodeIndex)
{
    disk.clear();

    Inode in = this->readInode(dirInodeIndex);
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

void BlockStorageEngine::addDirectoryEntry(const char *fileName, int dirInodeIndex, int targetInodeIndex)
{
    disk.flush();
    disk.clear();
    Inode in = this->readInode(dirInodeIndex);
    if (!in.isDirectory)
    {
        cerr << "Error: Inode at index " << dirInodeIndex << " is not a directory!" << endl;
        return;
    }

    if (this->findInDirectory(fileName, dirInodeIndex) != -1)
    {
        cerr << "Error: File with name '" << fileName << "' already exists in directory with inode index " << dirInodeIndex << "!" << endl;
        return;
    }

    DirectoryEntry newEntry;
    strncpy(newEntry.fileName, fileName, sizeof(newEntry.fileName) - 1);
    newEntry.fileName[sizeof(newEntry.fileName) - 1] = '\0'; // Ensure null-termination
    newEntry.inodeIndex = targetInodeIndex;

    long byteOffset;
    if (in.blockCount == 0 || in.lastBlockUsedBytes == sb.blockSize)
    {
        int blockId = this->findFreeBlock();
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
        int currentBlock = in.directBlocks[in.blockCount - 1];
        byteOffset = sb.dataRegionStart + (currentBlock * sb.blockSize) + in.lastBlockUsedBytes;
    }
    disk.seekp(byteOffset, ios::beg);
    disk.write(reinterpret_cast<char *>(&newEntry), sizeof(newEntry));
    disk.flush();

    in.lastBlockUsedBytes += sizeof(newEntry);
    this->writeInode(in, dirInodeIndex);
    this->updateSuperBlock();
}

void BlockStorageEngine::save(const string &fileName, const string &filePath)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int finalDirInodeIndex = this->traversePath(parentPath);
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
    this->preSaveCheck(fileSize);
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
    this->allocateBlock(fileInode, buffer);
    sb.allocatedInodeCount++;

    // update info saved in the disk
    this->updateSuperBlock();
    this->writeInode(fileInode, sb.allocatedInodeCount - 1);

    // add the file entry to it's parent folder
    this->addDirectoryEntry(rfile.c_str(), finalDirInodeIndex, sb.allocatedInodeCount - 1); // Add entry to root directory
    cout << "File '" << rfile << "' saved successfully!" << endl;
}

void BlockStorageEngine::retrieve(const char *fileName, const string &destPath)
{
    int inodeIndex = this->findInDirectory(fileName, 0);
    if (inodeIndex == -1)
    {
        cerr << "Error: File '" << fileName << "' not found in root directory!" << endl;
        return;
    }
    Inode recoveryInode = this->readInode(inodeIndex);

    vector<char> fileData = this->recoverFile(recoveryInode);
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
                if (ent.fileName == path[i])
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

// work on the create directory function for tommorrow

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
    sb.allocatedInodeCount++;
    this->writeInode(dir, sb.allocatedInodeCount - 1);
    this->addDirectoryEntry(lastElem.c_str(), finalDirInodeIndex, sb.allocatedInodeCount - 1);
    disk.flush();
}