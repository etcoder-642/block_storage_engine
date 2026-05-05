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

bool findFileInDirectory(const string &fileName, const string &dirPath)
{
    string path = dirPath; // Current directory
    string needle = dirPath + fileName + ".bin";
    for (const auto & entry : fs::directory_iterator(path)) {
        // Output the path of each file or directory
        if(entry.path() == needle)
          return true;
    }
    return false;
}


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

void BlockStorageEngine::allocateBlock(Inode &in, vector<char> &data)
{
    disk.flush();
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

        writeLocation = sb.dataRegionStart + (freeBlockID * sb.blockSize);
        in.blockCount++;
        in.lastBlockUsedBytes = amountToWrite;
        if (in.blockCount > 12)
        {
            cerr << "Error: Exceeded maximum direct blocks!" << endl;
            return;
        }
        in.directBlocks[in.blockCount - 1] = freeBlockID;
        disk.seekp(writeLocation, ios::beg);
        disk.write(dataPtr, amountToWrite);
        dataPtr += amountToWrite;
        writeLocation += amountToWrite;
        bytesRemaining -= amountToWrite;
        sb.freeBlockCount--;
    }
    disk.flush();
    disk.clear();
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

    for (int i = 0; i < in.blockCount; i++)
    {
        int amountToRead = bytesToRead < sb.blockSize ? bytesToRead : sb.blockSize;
        long readSpot = sb.dataRegionStart + (in.directBlocks[i] * sb.blockSize);
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
    const long MAX_SIZE = Inode::MAX_DIRECT_BLOCKS * sb.blockSize;
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

    string fullPath = baseDir + name + ".bin";
    if(findFileInDirectory(name, baseDir)){
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
    if(!findFileInDirectory(name, baseDir)){
        cerr << "This disk haven't been created! " << endl;
    }

    string diskPath = baseDir + name + ".bin";
    disk.open(diskPath, ios::in | ios::out | ios::binary);

    long diskSize = 0;
    disk.seekg(0, ios::end);
    diskSize = disk.tellg();
    disk.seekg(0, ios::beg);
    sb.blockCount = diskSize / sb.blockSize;
    this->updateSbInfo();
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

void BlockStorageEngine::save(const string &fileName, const string &filePath, const string &fileType)
{
    fstream fileToSave;
    fileToSave.open(filePath, ios::in | ios::out | ios::binary);
    if (!fileToSave.is_open())
    {
        cerr << "Error: Could not open file at " << filePath << endl;
        return;
    }

    long fileSize = 0;
    fileToSave.seekg(0, ios::end);
    fileSize = fileToSave.tellg();
    fileToSave.seekg(0, ios::beg);

    vector<char> buffer(fileSize);
    char *bufferPtr = buffer.data();
    fileToSave.read(bufferPtr, fileSize);

    fileToSave.close();

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

    Inode fileInode;
    fileInode.fileSize = buffer.size();
    fileInode.fileType[0] = '\0';
    strncpy(fileInode.fileType, fileType.c_str(), sizeof(fileInode.fileType) - 1);
    fileInode.fileType[sizeof(fileInode.fileType) - 1] = '\0'; // Ensure null-termination
    this->allocateBlock(fileInode, buffer);
    sb.allocatedInodeCount++;
    this->updateSuperBlock();
    this->writeInode(fileInode, sb.allocatedInodeCount - 1);

    this->addDirectoryEntry(fileName.c_str(), 0, sb.allocatedInodeCount - 1); // Add entry to root directory
    cout << "File '" << fileName << "' saved successfully!" << endl;
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
