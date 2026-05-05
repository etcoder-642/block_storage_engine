#ifndef API_H
#define API_H

#include <iostream>
#include <fstream>
#include <vector>

using namespace std;

enum class AllocError
{
    OK,
    EXCEEDS_MAX_FILE_SIZE,
    DISK_FULL,
    FILE_NOT_FOUND,
    CANNOT_CREATE_FILE,
};

struct DiskMap
{
    unsigned char bitmap[8] = {0}; // 8 bits per byte = 8 bytes for the bitmap
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
    static constexpr int MAX_DIRECT_BLOCKS = 24; // Maximum number of direct blocks
    int fileSize = 0;
    int blockCount = 0;
    int directBlocks[MAX_DIRECT_BLOCKS] = {-1};
    int lastBlockUsedBytes = 0; // To track how many bytes are used in the last block
    char fileType[16] = {0}; // For simplicity, we can store file type as a string (e.g., "txt", "jpg")

    bool isDirectory = false;
    char padding[3] = {0}; // Padding to ensure the struct is exactly 128 bytes
};
static_assert(sizeof(Inode) == 128, "Inode size mismatch");

struct DirectoryEntry
{
    static constexpr int MAX_FILE_NAME_LENGTH = 60;
    char fileName[MAX_FILE_NAME_LENGTH] = {0};
    int inodeIndex;
};

static_assert(sizeof(DirectoryEntry) == 64, "DirectoryEntry size mismatch");

class BlockStorageEngine
{
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
    void preSaveCheck(long dataSize);
    void addDirectoryEntry(const char* fileName, int dirInodeIndex, int targetInodeIndex);
    int findInDirectory(const char* fileName, int dirInodeIndex);
    void updateSuperBlock();
    void updateSbInfo();
    void updateBitMap();

public:
    void createDisk(const string &name, long sizeInMB);
    void mountDisk(const string &name);
    void unmountDisk();
    void printBitMap();

    void save(const string &fileName, const string &filePath, const string &fileType);
    void retrieve(const char* fileName, const string &destPath);
    void remove(const string &fileName);
    void list();
};

#endif // API_H