#ifndef API_H
#define API_H

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

enum class AllocError {
    OK,
    EXCEEDS_MAX_FILE_SIZE,
    DISK_FULL,
    FILE_NOT_FOUND,
    CANNOT_CREATE_FILE,
};

struct DiskMap
{
    unsigned char bitmap[8];
};

struct SuperBlock
{
    int magicNumber = 12345;
    int blockSize = 4096;
    int blockCount = 0;

    int bitmapSize = blockCount / 8;
    int inodeCount = 64;
    int inodeSize = 128; // Size of each inode in bytes
    int allocatedInodeCount = 0;
    int freeBlockCount = blockCount;

    int bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
    int inodeTableStart = bitmapStart + bitmapSize;
    int dataRegionStart = inodeTableStart + (inodeSize * inodeCount);
};

struct Inode
{
    static constexpr int MAX_DIRECT_BLOCKS = 28; // Maximum number of direct blocks
    int fileSize = 0;
    int blockCount = 0;
    int directBlocks[MAX_DIRECT_BLOCKS] = {-1};

    bool isDirectory = false;
    char padding[7] = {0}; // Padding to ensure the struct is exactly 128 bytes
};
static_assert(sizeof(Inode) == 128, "Inode size mismatch");

struct DirectoryEntry
{
    char fileName[32];
    int inodeIndex;
};

static_assert(sizeof(DirectoryEntry) == 36, "DirectoryEntry size mismatch");

class BlockStorageEngine {
private:
    fstream disk;
    SuperBlock sb;
    DiskMap bitmap;
    AllocError allocError = AllocError::OK;

    bool isBlockFree(int index);
    void setBlockOccupied(int index);
    long calculateTotalSize();
    int findFreeBlock();
    void allocateBlock(Inode &in, vector<char> &data);
    void writeInode(Inode &in, int inodeIndex);
    Inode readInode(int inodeIndex);
    void formatDisk();
    vector<char> recoverFile(Inode &in);
    void printBitMap();
    void preSaveCheck(long dataSize);
    void addDirectoryEntry(const char fileName[32], int inodeIndex);

public:
    void createDisk(string &path, long sizeInMB);
    void mountDisk(string &path);
    void unmountDisk();

    void save(string &fileName, const string &filePath);
    void retrieve(string &fileName, const string &destPath);
    void remove(const string &fileName);
    void list();
};

#endif // API_H