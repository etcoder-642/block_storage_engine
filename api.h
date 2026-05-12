#ifndef API_H
#define API_H

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;
/*
  Currently the indirectBlock isn't supported for directory inodes
  It must be supported.
*/

enum class AllocError
{
    OK,
    EXCEEDS_MAX_FILE_SIZE,
    DISK_FULL,
    FILE_NOT_FOUND,
    CANNOT_CREATE_FILE,
};

struct SuperBlock
{
    static constexpr const char* DISK_EXTENSION = ".fdb";
    static constexpr int MAGIC_NUMBER = 0x406EDB;

    int magicNumber = MAGIC_NUMBER;
    int blockSize = 4096;
    int blockCount = 0;

    int bitmapSize = blockCount / 8;
    int inodeCount = 64;
    int allocatedInodeCount = 0;
    int freeBlockCount = blockCount;

    int bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
    int inodeTableStart = bitmapStart + bitmapSize;
    int dataRegionStart = inodeTableStart + (sizeof(Inode) * inodeCount);
};

struct Inode
{
    static constexpr int MAX_DIRECT_BLOCKS = 24; // Maximum number of direct blocks
    int fileSize = 0;
    int blockCount = 0;
    int directBlocks[MAX_DIRECT_BLOCKS] = {-1};
    int lastBlockUsedBytes = 0; // To track how many bytes are used in the last block

    // points to a block that contains blockIndexes of data.
    int indirectBlocks = -1;
    bool isDirectory = false;
    bool isAllocated = false;
    int referenceCount = 0;
    char padding[8] = {0}; // Padding to ensure the struct is exactly 128 bytes
};
static_assert(sizeof(Inode) == 128, "Inode size mismatch");


struct DirectoryEntry
{
    static constexpr int MAX_FILE_NAME_LENGTH = 60;
    char fileName[MAX_FILE_NAME_LENGTH] = {0};
    int inodeIndex;
};

static_assert(sizeof(DirectoryEntry) == 64, "DirectoryEntry size mismatch");

// Main class
class BlockStorageEngine
{
private:
    fstream disk;
    SuperBlock sb;
    AllocError allocError = AllocError::OK;

    // bitmap operations
    bool isBlockFree(int index);
    void setBlockOccupied(int index);

    // disk geometry and formatting
    long calculateTotalSize();
    void formatDisk();
    void syncSuperBlock();
    void updateSbInfo();
    void updateBitMap();

    // allocation
    int findFreeBlock();
    int findFreeInode();
    void allocateBlock(Inode &in, vector<char> &data);
    void allocateIndirectBlock(Inode &in, vector<char> &data, int bytesRemaining);

    // read from disk
    vector<char> recoverFile(Inode &in);
    Inode readInode(int inodeIndex);
    DirectoryEntry readDirectoryEntry(int dirEntryIndex, int blockIndex);

    // write to disk
    void writeDataToBlock(int blockID, char* dataPtr, int amountToWrite, int& bytesRemaining);
    void writeInode(Inode &in, int inodeIndex);
    void addDirectoryEntry(const char* fileName, int dirInodeIndex, int targetInodeIndex);

    // validation
    void preSaveCheck(long dataSize);

    // search and traversal
    int findInDirectory(const char* fileName, int dirInodeIndex);
    int traversePath(vector<string> path);
    int findDirEntry(int inodeIndex, const char* fileName);

public:
    // special function for debugging purposes
    void printBitMap();

    // create/ initiate/ discontinue a disk instance
    void createDisk(const string &name, long sizeInMB);
    void mountDisk(const string &name);
    void unmountDisk();

    // creation or deletion of files/dirs
    void save(const string &fileName, const string &filePath);
    void remove(const string &fileName);
    void createDirectory(const string &path);
    // void removeDirectory(const string &path); // removes a directory with all it's contents

    // retrieval of files
    void retrieve(const string &fileName, const string &destPath);

    // auxiliary / supporting functions
    void link(const char* fileN, const char* sfileN);
    void list();
};

#endif // API_H