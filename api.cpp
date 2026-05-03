#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <cstring>
namespace fs = std::filesystem;


#include "api.h"

using namespace std;

string getAppDirectory() {
#ifdef _WIN32
    // C:\Users\name\AppData\Roaming\BlockEngine\
    const char* appData = getenv("APPDATA");
    if (!appData) throw runtime_error("APPDATA environment variable not found");
    return string(appData) + "\\BlockEngine\\";

#elif __APPLE__
    // /Users/name/Library/Application Support/BlockEngine/
    const char* home = getenv("HOME");
    if (!home) throw runtime_error("HOME environment variable not found");
    return string(home) + "/Library/Application Support/BlockEngine/";

#else
    // Linux: /home/name/.local/share/BlockEngine/
    // Respects XDG standard if set, falls back to HOME
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg) return string(xdg) + "/BlockEngine/";

    const char* home = getenv("HOME");
    if (!home) throw runtime_error("HOME environment variable not found");
    return string(home) + "/.local/share/BlockEngine/";
#endif
}

bool BlockStorageEngine::isBlockFree(int index){
    disk.flush();
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

void BlockStorageEngine::setBlockOccupied(int index){
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
    sb.freeBlockCount--;

    disk.flush(); // Ensure the updated bitmap is written to disk
}

long BlockStorageEngine::calculateTotalSize(){
    long totalSize = sb.blockSize * sb.blockCount; // Size of all blocks
    totalSize += sb.bitmapSize;                    // Add bitmap size
    totalSize += sb.inodeSize * sb.inodeCount;     // Add size of all inodes
    return totalSize;
}

int BlockStorageEngine::findFreeBlock(){
    int index = 0;

    // "Skip" all blocks that are already taken
    while (!isBlockFree(index)) {
        index++;
        if (index >= sb.blockCount) {
            return -1; // Standard way to signal "Disk Full"
        }
    }

    // Now index is pointing to a free block!
    setBlockOccupied(index);
    return index;
}

void BlockStorageEngine::allocateBlock(Inode &in, vector<char> &data){
    disk.flush();
    disk.clear();

    if(in.isDirectory){
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

        int freeBlockID = findFreeBlock();
        if(freeBlockID == -1) {
            cerr << "Error: Disk is full, cannot allocate more blocks!" << endl;
            return;
        }

        writeLocation = sb.dataRegionStart + (freeBlockID * sb.blockSize);
        in.blockCount++;
        if(in.blockCount > 12) {
            cerr << "Error: Exceeded maximum direct blocks!" << endl;
            return;
        }
        in.directBlocks[in.blockCount - 1] = freeBlockID;
        disk.seekp(writeLocation, ios::beg);
        disk.write(dataPtr, amountToWrite);
        dataPtr += amountToWrite;
        writeLocation += amountToWrite;
        bytesRemaining -= amountToWrite;
    }
    disk.flush();
    disk.clear();

}

void BlockStorageEngine::writeInode(Inode &in, int inodeIndex){
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char*>(&in), sb.inodeSize);
    sb.allocatedInodeCount++;
    disk.flush();
}

Inode BlockStorageEngine::readInode(int inodeIndex){
    Inode myInode;
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char*>(&myInode), sb.inodeSize);
    disk.clear();
    return myInode;
}

void BlockStorageEngine::formatDisk(){
    disk.seekp(0, ios::beg);
    char zero = 0;
    for (long i = 0; i < calculateTotalSize(); i++)
    {
        disk.write(&zero, 1);
    }
}

vector<char> BlockStorageEngine::recoverFile(Inode &in){
    disk.flush();
    disk.clear();

    if(in.isDirectory){
        cerr << "Error: Cannot recover a directory as a file!" << endl;
        return vector<char>();
    }

    vector<char> recoveredFile(in.fileSize);
    int bytesToRead = in.fileSize;
    char* writePtr = recoveredFile.data();

    for(int i = 0; i < in.blockCount; i++){
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

void BlockStorageEngine::printBitMap(){
    disk.flush();
    disk.clear();

    cout << "Bitmap Status: " << endl;
    for (int i = 0; i < sb.blockCount; i++) {
        cout << (isBlockFree(i) ? "0" : "1") << ' ';
        if((i + 1) % 64 == 0) {
            cout << endl; // New line after every 64 blocks for better readability
        }
    }
}


void BlockStorageEngine::createDisk(string &path, long sizeInMB){
    string baseDir = getAppDirectory();
    fs::create_directories(baseDir);
    
    string fullPath = baseDir + path + ".bin";
    {
        ofstream create(fullPath, ios::binary);
    }
    disk.open(fullPath, ios::in | ios::out | ios::binary | ios::trunc);

    if(!disk.is_open()){
        allocError = AllocError::CANNOT_CREATE_FILE;
        cerr << "Error: Could not create disk file at " << fullPath << endl;
        return;
    }

    cout << "Disk created at: " << fullPath << endl;
    this->formatDisk();
    disk.write(reinterpret_cast<char *>(&sb), sizeof(SuperBlock));
    disk.write(reinterpret_cast<char *>(&bitmap), sizeof(DiskMap));
    sb.blockCount = (sizeInMB * 1024 * 1024) / sb.blockSize;
    disk.flush();

}

void BlockStorageEngine::preSaveCheck(long dataSize) // In Bytes
{
    // checking for total blocks needed
    long blockNeeded = (dataSize/sb.blockSize);
    blockNeeded += (dataSize%sb.blockSize == 0) ? 0: 1;
    if(blockNeeded > sb.freeBlockCount){
        allocError = AllocError::DISK_FULL;
        return;
    }

    // checking for MAX_FILE_SIZE  
    const long MAX_SIZE = Inode::MAX_DIRECT_BLOCKS * sb.blockSize;
    if(dataSize > MAX_SIZE){
        allocError = AllocError::EXCEEDS_MAX_FILE_SIZE;
        return;
    }
}

void BlockStorageEngine::addDirectoryEntry(const char fileName[32], int inodeIndex)
{
    disk.flush();
    disk.clear();
    Inode sampleInode = this->readInode(inodeIndex);
    if(!sampleInode.isDirectory)
    {
        cerr << "Error: Inode at index " << inodeIndex << " is not a directory!" << endl;
        return;
    }

    DirectoryEntry newEntry;
    strncpy(newEntry.fileName, fileName, sizeof(newEntry.fileName) - 1);
    newEntry.fileName[sizeof(newEntry.fileName) - 1] = '\0'; // Ensure null-termination
    newEntry.inodeIndex = inodeIndex;
}

void BlockStorageEngine::save(string &fileName, const string &filePath)
{
    {
        ofstream create(filePath, ios::binary);
    }
    fstream fileToSave;
    fileToSave.open(filePath, ios::in | ios::out | ios::binary);

    long fileSize = 0;
    fileToSave.seekg(0, ios::end);
    fileSize = fileToSave.tellg();
    fileToSave.seekg(0, ios::beg);

    vector<char> buffer;
    char* bufferPtr = buffer.data();
    fileToSave.read(bufferPtr, fileSize);

    fileToSave.close();

    this->preSaveCheck(fileSize);
    if(allocError != AllocError::OK) {
        if(allocError == AllocError::DISK_FULL) {
            cerr << "Error: Not enough free space on disk to save file!" << endl;
        } else if(allocError == AllocError::EXCEEDS_MAX_FILE_SIZE) {
            cerr << "Error: File size exceeds maximum allowed size of " << (Inode::MAX_DIRECT_BLOCKS * sb.blockSize) << " bytes!" << endl;
        }
        return;
    }

    Inode fileInode;
    fileInode.fileSize = buffer.size();
    this->allocateBlock(fileInode, buffer);
    this->writeInode(fileInode, sb.allocatedInodeCount);
    cout << "File '" << fileName << "' saved successfully!" << endl;
}

void BlockStorageEngine::retrieve(string &fileName, const string &destPath)
{
    // Inode recoveryInode = 
}
