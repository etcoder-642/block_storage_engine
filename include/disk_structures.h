#ifndef DISK_STRUCTURES_H
#define DISK_STRUCTURES_H

#include <iostream>
#include <fstream>
#include <vector>

#include "error/result.h"
#include "error/error.h"

namespace bse
{

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

    struct SuperBlock
    {
        static constexpr const char *DISK_EXTENSION = ".fdb";
        static constexpr int MAGIC_NUMBER = 0x406EDB;

        int magicNumber = MAGIC_NUMBER;
        int blockSize = 4096;
        int blockCount = 0;

        int bitmapSize = blockCount / 8;
        int inodeCount = 0;
        int allocatedInodeCount = 0;
        int freeBlockCount = blockCount;

        int bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
        int inodeTableStart = bitmapStart + bitmapSize;
        int dataRegionStart = inodeTableStart + (sizeof(Inode) * inodeCount);
    };

    struct DirectoryEntry
    {
        static constexpr int MAX_FILE_NAME_LENGTH = 59;
        char fileName[MAX_FILE_NAME_LENGTH] = {0};
        bool isAllocated = false;
        int inodeIndex;
    };

    static_assert(sizeof(DirectoryEntry) == 64, "DirectoryEntry size mismatch");

}

#endif