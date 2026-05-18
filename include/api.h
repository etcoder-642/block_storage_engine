#ifndef API_H
#define API_H

#include <iostream>
#include <fstream>
#include <vector>

#include "error/result.h"
#include "error/error.h"
#include "disk_structures.h"

/*
  Currently the indirectBlock isn't supported for directory inodes
  It must be supported.
*/

namespace bse
{
    // Main class
    class BlockStorageEngine
    {
    private:
        fstream disk;
        SuperBlock sb;
        AllocError allocError = AllocError::OK;

        // bitmap operations
        Result<void> isBlockFree(int index);
        void setBitOccupied(int index);
        void setBitFree(int index);

        // disk geometry and formatting
        long calculateTotalSize();
        void formatDisk(int size, int initialPos = 0);
        void syncSuperBlock();
        void updateSbInfo();

        // allocation
        Result<int> findFreeBlock();
        Result<int> findFreeInode();
        Result<int> findFreeDirEntry(const int *directBlocks, int blockCount);

        Result<void> freeBlock(int index);
        Result<void> freeIndirectBlocks(int index, int usedCount);
        void freeDirectoryEntry(int dirEntryIndex, int dirInodeIndex);
        Result<void> allocateBlock(Inode &in, std::vector<char> &data);
        Result<void> allocateIndirectBlock(Inode &in, std::vector<char> &data, int bytesRemaining);

        // read from disk
        Result<std::vector<char>> recoverFile(Inode &in);
        Result<Inode> readInode(int inodeIndex);
        Result<DirectoryEntry> readDirectoryEntry(int dirEntryOffset, int blockIndex);

        // write to disk
        Result<void> writeDataToBlock(int blockID, char *dataPtr, int amountToWrite, int &bytesRemaining);
        Result<void> writeInode(Inode &in, int inodeIndex);
        Result<void> writeDirEntry(DirectoryEntry &ent, int dirEntryOffset, int blockIndex);
        Result<void> addDirectoryEntry(const char *fileName, int dirInodeIndex, int targetInodeIndex);

        // validation
        Result<void> preSaveCheck(long dataSize);

        // search and traversal
        Result<int> findInDirectory(const char *entityName, int dirInodeIndex);
        Result<int> traversePath(std::vector<std::string> path);
        Result<int> findDirEntry(int dirInodeIndex, const char *fileName); // returns dirEntryIndex (not dirEntryOffset)

    public:
        // special function for debugging purposes
        void printBitMap();

        // create/ initiate/ discontinue a disk instance
        Result<void> createDisk(const std::string &name, long sizeInMB);
        Result<void> mountDisk(const std::string &name);
        Result<void> unmountDisk();

        // creation or deletion of files/dirs
        Result<void> save(const std::string &fileName, const std::string &filePath, int inodeIndex = -1);
        Result<void> remove(const std::string &fileName);
        Result<void> createDirectory(const std::string &path);
        Result<void> removeDirectory(const std::string &path); // removes a directory with all it's contents

        // retrieval of files
        Result<void> retrieve(const std::string &fileName, const std::string &destPath);

        // auxiliary / supporting functions
        Result<void> link(const std::string &nfile, const std::string &efile); // nfile: new file name, efile: existing file name
        Result<void> list(std::string path);
        Result<void> move(const std::string &file, const std::string &dPath);        // dPath: destination path, file: file to move
        Result<void> rename(const std::string &file, const std::string &nName);      // nName: new file name, file: file to rename
        Result<void> replace(const std::string &file, const std::string &nfilePath); // nfilePath: new file path, file: file to replace
        void diskInfo();
        Result<void> printFileStructure(int dirInodeIndex = 0, int depth = 0);
    };

}

enum class AllocError
{
    OK,
    EXCEEDS_MAX_FILE_SIZE,
    DISK_FULL,
    FILE_NOT_FOUND,
    CANNOT_CREATE_FILE,
};

#endif // API_H