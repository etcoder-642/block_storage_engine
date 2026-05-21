#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <cstring>

#include "../include/api.h"
#include "../include/error/error.h"
#include "../include/error/result.h"
#include "../include/disk_structures.h"

#define TRY(expr, ReturnType) ({                       \
    auto _r = (expr);                                  \
    if (_r.isErr())                                    \
    {                                                  \
        auto _e = _r.unwrapErr();                      \
        _e.pushFrame(std::source_location::current()); \
        return Result<ReturnType>::Err(_e);            \
    }                                                  \
    _r.unwrap();                                       \
})

#define TEST(expr) ({                                  \
    auto _r = (expr);                                  \
    if (_r.isErr())                                    \
    {                                                  \
        auto _e = _r.unwrapErr();                      \
        _e.pushFrame(std::source_location::current()); \
        return Result<void>::Err(_e);                  \
    }                                                  \
})

using namespace std;
using namespace bse;
namespace fs = std::filesystem;

// HELPER FUNCTIONS
// returns a specific directory where disk will be stored (e.g. "/.local/share/BlockEngine/" in Linux)
Result<string> getAppDirectory()
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
        return Result<string>::Ok(string(xdg) + "/BlockEngine/");

    const char *home = getenv("HOME");
    if (!home)
    {
        return Result<string>::Err(BSError::make(
            ErrorCode::HOME_ENVIRONMENT_VARIABLE_NOT_FOUND,
            "Error: Home environment variable not found!"));
    }
    return Result<string>::Ok(string(home) + "/.local/share/BlockEngine/");
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
Result<void> BlockStorageEngine::isBlockFree(int bitPosition)
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
        return Result<void>::Err(BSError::make(
            ErrorCode::BLOCK_ALREADY_OCCUPIED,
            "Error: Block is already occupied!"));
    }
    else
        return Result<void>::Ok();
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
    return;
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
    return;
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
    return;
}

// syncs in-memory superBlock data with the disk
void BlockStorageEngine::syncSuperBlock()
{
    disk.seekp(0, ios::beg);
    disk.write(reinterpret_cast<char *>(&sb), sizeof(SuperBlock));
    disk.flush();
    return;
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
    return;
}

// ALLOCATION
Result<int> BlockStorageEngine::findFreeBlock()
{
    int index = 0;

    // "Skip" all blocks that are already taken
    while (!isBlockFree(index).isOk())
    {
        index++;
        if (index >= sb.blockCount)
        {
            return Result<int>::Err(BSError::make(
                ErrorCode::DISK_FULL,
                "Error: Disk is full, cannot allocate more blocks!"));
        }
    }

    setBitOccupied(index);
    return Result<int>::Ok(index);
}

Result<int> BlockStorageEngine::findFreeInode()
{
    for (int i = 0; i < sb.inodeCount; i++)
    {
        Inode in = TRY(readInode(i), int);
        if (!in.isAllocated)
        {
            return Result<int>::Ok(i);
        }
    }
    if (sb.allocatedInodeCount >= sb.inodeCount)
        return Result<int>::Err(BSError::make(
            ErrorCode::DISK_FULL,
            "Error: Disk is full, cannot allocate more inodes!"));
    return Result<int>::Ok(sb.allocatedInodeCount); // return next free slot
}

Result<int> BlockStorageEngine::findFreeDirEntry(const int *directBlocks, int blockCount)
{
    int index = 0;
    for (int i = 0; i < blockCount; i++)
    {
        for (long unsigned int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent;
            long pos = sb.dataRegionStart + (directBlocks[i] * sb.blockSize) + (j * sizeof(DirectoryEntry));
            disk.seekg(pos, ios::beg);
            disk.read(reinterpret_cast<char *>(&ent), sizeof(DirectoryEntry));
            if (!ent.isAllocated)
            {
                return Result<int>::Ok(index);
            }
            index++;
        }
    }
    return Result<int>::Err(BSError::make(
        ErrorCode::DIRENT_NOT_FOUND,
        "Error: Directory Entry not found!"));
}

Result<void> BlockStorageEngine::freeDirectoryEntry(int dirEntryIndex, int dirInodeIndex)
{
    if (dirInodeIndex < 0 || dirInodeIndex >= sb.inodeCount)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_INODE_INDEX,
            "Error: Invalid Inode Index!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.inodeCount - 1) + "."));
    }
    Inode in = TRY(readInode(dirInodeIndex), void);
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);

    int dirEntBlock = dirEntryIndex / dirEntryPerBlock;
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;
    int blockIndex = in.directBlocks[dirEntBlock];

    DirectoryEntry ent = TRY(readDirectoryEntry(dirEntOffset, blockIndex), void);
    ent.isAllocated = false;
    ent.inodeIndex = -1;
    ent.fileName[0] = '\0';
    TEST(writeDirEntry(ent, dirEntOffset, blockIndex));
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::freeBlock(int index)
{
    if (index < 0 || index >= sb.blockCount)
    {
        cout << "freeBlock | Error: Invalid Block Index!" << endl;
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_BLOCK_INDEX,
            "Error: Invalid Block Index!",
            "Suggestion: Use an index more than 0 and less than " + std::to_string(sb.blockCount) + "."));
    }
    int pos = sb.dataRegionStart + (index * sb.blockSize);
    formatDisk(sb.blockSize, pos);
    setBitFree(index);
    sb.freeBlockCount++;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::freeIndirectBlocks(int index, int usedCount)
{
    if (index < 0 || index >= sb.blockCount)
    {
        cout << "freeIndirectBlocks | Error: Invalid Block Index!" << endl;
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_BLOCK_INDEX,
            "Error: Invalid Block Index!",
            "Suggestion: Use an index more than 0 and less than " + std::to_string(sb.blockCount) + "."));
    }

    int pos = sb.dataRegionStart + (index * sb.blockSize);
    int intsInBlock = sb.blockSize / sizeof(int);
    int blockIds[intsInBlock];
    disk.seekg(sb.dataRegionStart + (index * sb.blockSize), ios::beg);
    disk.read(reinterpret_cast<char *>(&blockIds), sb.blockSize);
    for (int i = 0; i < usedCount; i++)
    {
        TEST(freeBlock(blockIds[i]));
    }
    formatDisk(sb.blockSize, pos);
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::allocateBlock(Inode &in, vector<char> &data)
{
    disk.clear();

    if (in.isDirectory)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::BLOCK_ALLOCATION_ERROR,
            "Error: Cannot allocate blocks for a directory!",
            "Suggestion: Inode must be a file."));
    }

    char *dataPtr = data.data();
    int bytesRemaining = data.size();
    int blockSize = sb.blockSize;
    while (bytesRemaining > 0)
    {
        int amountToWrite = (blockSize < bytesRemaining) ? blockSize : bytesRemaining;
        int freeBlockID = TRY(findFreeBlock(), void);

        if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
        {
            in.indirectBlocks = freeBlockID;
            sb.freeBlockCount--;
            TEST(allocateIndirectBlock(in, data, bytesRemaining));
            return Result<void>::Ok();
        }
        TEST(writeDataToBlock(freeBlockID, dataPtr, amountToWrite, bytesRemaining));
        dataPtr += amountToWrite;
        in.blockCount++;
        in.lastBlockUsedBytes = amountToWrite;
        in.directBlocks[in.blockCount - 1] = freeBlockID;
    }
    disk.flush();
    disk.clear();
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::allocateIndirectBlock(Inode &in, vector<char> &data, int bytesRemaining)
{
    char *dataPtr = data.data() + (data.size() - (size_t)bytesRemaining);
    int blockSize = sb.blockSize;
    long writeLocation = 0;
    while (bytesRemaining > 0)
    {
        int amountToWrite = (blockSize < bytesRemaining) ? blockSize : bytesRemaining;

        int freeBlockID = TRY(findFreeBlock(), void);

        long unsigned int blocksInTheIndirectBlock = in.blockCount - Inode::MAX_DIRECT_BLOCKS;
        if (blocksInTheIndirectBlock >= sb.blockSize / sizeof(int))
        {
            cout << "allocateIndirectBlock | Error: indirect block full, file too large!" << endl;
            return Result<void>::Err(BSError::make(
                ErrorCode::FILE_TOO_LARGE,
                "Error: File too large!"));
        }
        in.blockCount++;

        // go to the block pointed by the indirect Block and write the freeBlock we just discovered
        writeLocation = sb.dataRegionStart + (in.indirectBlocks * sb.blockSize) + (blocksInTheIndirectBlock * sizeof(int));
        disk.seekp(writeLocation, ios::beg);
        disk.write(reinterpret_cast<char *>(&freeBlockID), sizeof(int));

        // now we continue writing our data
        in.lastBlockUsedBytes = amountToWrite;
        TEST(writeDataToBlock(freeBlockID, dataPtr, amountToWrite, bytesRemaining));
        dataPtr += amountToWrite;
    }
    disk.flush();
    return Result<void>::Ok();
}

// READ FROM DISK
// NOT_A_DIRECTORY
Result<Inode> BlockStorageEngine::readInode(int inodeIndex)
{
    if (inodeIndex < 0 || inodeIndex >= sb.inodeCount)
    {
        return Result<Inode>::Err(BSError::make(
            ErrorCode::INVALID_INODE_INDEX,
            "Error: Inode Index out of range!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.inodeCount - 1) + "."));
    }
    Inode myInode;
    long pos = sb.inodeTableStart + (inodeIndex * sizeof(Inode));
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myInode), sizeof(Inode));
    disk.clear();
    return Result<Inode>::Ok(myInode);
}

Result<vector<char>> BlockStorageEngine::recoverFile(Inode &in)
{
    disk.flush();
    disk.clear();

    if (in.isDirectory)
    {
        return Result<vector<char>>::Err(BSError::make(
            ErrorCode::EXPECTED_A_FILE,
            "Error: Cannot recover a directory as a file!"));
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
    return Result<vector<char>>::Ok(recoveredFile);
}

// dirEntryIndex is the index of the DirectoryEntry (index inside a single block)
Result<DirectoryEntry> BlockStorageEngine::readDirectoryEntry(int dirEntryOffset, int blockIndex)
{
    if (dirEntryOffset < 0 || dirEntryOffset >= sb.blockSize / sizeof(DirectoryEntry))
    {
        return Result<DirectoryEntry>::Err(BSError::make(
            ErrorCode::INVALID_DIR_ENTRY_OFFSET,
            "Error: Invalid Directory Entry Offset!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.blockSize / sizeof(DirectoryEntry) - 1) + "."));
    }

    disk.clear();
    DirectoryEntry myDirEntry;
    long pos = sb.dataRegionStart + (sb.blockSize * blockIndex) + (dirEntryOffset * sizeof(DirectoryEntry));
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char *>(&myDirEntry), sizeof(myDirEntry));
    return Result<DirectoryEntry>::Ok(myDirEntry);
}

// WRITE TO DISK
Result<void> BlockStorageEngine::writeDataToBlock(int blockID, char *dataPtr, int amountToWrite, int &bytesRemaining)
{
    if (blockID < 0 || blockID >= sb.blockCount)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_BLOCK_INDEX,
            "Error: Block not found!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.blockCount - 1) + "."));
    }

    long writeLocation = sb.dataRegionStart + (blockID * sb.blockSize);
    disk.seekp(writeLocation, ios::beg);
    disk.write(dataPtr, amountToWrite);
    bytesRemaining -= amountToWrite;
    sb.freeBlockCount--;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::writeInode(Inode &in, int inodeIndex)
{
    if (inodeIndex < 0 || inodeIndex >= sb.inodeCount)
    {
        return Result<void>::Err(
            ErrorCode::INVALID_INODE_INDEX,
            "Error: Inode Index out of range!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.inodeCount - 1) + ".");
    }

    long pos = sb.inodeTableStart + (inodeIndex * sizeof(Inode));
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char *>(&in), sizeof(Inode));
    disk.flush();

    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::writeDirEntry(DirectoryEntry &ent, int dirEntryOffset, int blockIndex)
{
    if (dirEntryOffset < 0 || dirEntryOffset >= sb.blockSize / sizeof(DirectoryEntry))
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_DIR_ENTRY_OFFSET,
            "Error: Invalid Directory Entry Offset!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.blockSize / sizeof(DirectoryEntry) - 1) + "."));
    }
    else if (blockIndex < 0 || blockIndex >= sb.blockCount)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::INVALID_BLOCK_INDEX,
            "Error: Invalid Block Index!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.blockCount - 1) + "."));
    }

    disk.clear();
    long pos = sb.dataRegionStart + (blockIndex * sb.blockSize) + dirEntryOffset * sizeof(DirectoryEntry);
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char *>(&ent), sizeof(DirectoryEntry));
    disk.flush();

    return Result<void>::Ok();
}

// writes a new directory entry into the parent directory's block
Result<void> BlockStorageEngine::addDirectoryEntry(const char *fileName, int dirInodeIndex, int targetInodeIndex)
{
    disk.flush();
    disk.clear();
    Inode in = TRY(readInode(dirInodeIndex), void);
    if (!in.isDirectory)
    {
        return Result<void>::Err(
            ErrorCode::EXPECTED_A_DIRECTORY,
            "Error: Inode at index " + std::to_string(dirInodeIndex) + " is not a directory!",
            "Suggestion: Use an index between 0 and " + std::to_string(sb.inodeCount - 1) + ".");
    }

    if (findInDirectory(fileName, dirInodeIndex).isOk())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DIRECTORY_ALREADY_EXISTS,
            "Error: Directory already exists at inode index " + std::to_string(dirInodeIndex)));
    };

    if (in.blockCount >= Inode::MAX_DIRECT_BLOCKS)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DIRECTORY_FULL,
            "Error: Directory has reached maximum entry limits!",
            "Suggestion: Number of entries in a directory cannot exceed " + std::to_string(Inode::MAX_DIRECT_BLOCKS) + "."));
    }

    DirectoryEntry newEntry;
    strncpy(newEntry.fileName, fileName, sizeof(newEntry.fileName) - 1);
    newEntry.fileName[sizeof(newEntry.fileName) - 1] = '\0'; // Ensure null-termination
    newEntry.inodeIndex = targetInodeIndex;
    newEntry.isAllocated = true;

    // find a free directory entry
    auto _freeDirEntry = findFreeDirEntry(in.directBlocks, in.blockCount);
    int dirEntryIndex = _freeDirEntry.isOk() ? _freeDirEntry.unwrap() : -1;

    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);

    int dirEntBlock = 0;
    int dirEntOffset = 0;

    long byteOffset; // write position

    // if the directory entry is already allocated, find the block and offset
    if (dirEntryIndex != -1 && _freeDirEntry.isOk())
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
            int blockId = TRY(findFreeBlock(), void);
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
    TEST(writeInode(in, dirInodeIndex));
    syncSuperBlock();
    return Result<void>::Ok();
}

// VALIDATION
Result<void> BlockStorageEngine::preSaveCheck(long dataSize) // In Bytes
{
    // checking for total blocks needed
    long blockNeeded = (dataSize / sb.blockSize);
    blockNeeded += (dataSize % sb.blockSize == 0) ? 0 : 1;
    if (blockNeeded > sb.freeBlockCount)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DISK_FULL,
            "Error: Disk is full"));
    }

    // checking for MAX_FILE_SIZE
    const int PTR_SIZE = sizeof(int);
    const int PTRS_PER_BLOCK = sb.blockSize / PTR_SIZE;
    const long MAX_SIZE = (Inode::MAX_DIRECT_BLOCKS * sb.blockSize) + (sb.blockSize * PTRS_PER_BLOCK);
    if (dataSize > MAX_SIZE)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::EXCEEDS_MAX_FILE_SIZE,
            "Error: Exceeds Maximum file size permitted!",
            "Suggestion: Maximum file size permitted is " + std::to_string(MAX_SIZE) + " bytes."));
    }
    return Result<void>::Ok();
}

// SEARCH AND TRAVERSAL
// searches for a file inside a directory, returns the inode index of the file, -1 if not found
Result<int> BlockStorageEngine::findInDirectory(const char *entityName, int dirInodeIndex)
{
    disk.clear();

    Inode in = TRY(readInode(dirInodeIndex), int);

    int lastUsedBlock = in.blockCount - 1;
    if (lastUsedBlock == -1)
    {
        return Result<int>::Err(BSError::make(
            ErrorCode::DIRENT_NOT_FOUND,
            "Error: Directory Entry not found!")); // Directory is empty, so file cannot be found
    }
    for (int i = 0; i <= lastUsedBlock; i++)
    {
        int blockSize = sb.blockSize;
        if (i == lastUsedBlock && in.lastBlockUsedBytes > 0)
        {
            blockSize = in.lastBlockUsedBytes;
        }
        for (long unsigned int j = 0; j < blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry entry;
            disk.seekg(sb.dataRegionStart + (in.directBlocks[i] * sb.blockSize) + (j * sizeof(DirectoryEntry)), ios::beg);
            disk.read(reinterpret_cast<char *>(&entry), sizeof(DirectoryEntry));
            if (strncmp(entry.fileName, entityName, DirectoryEntry::MAX_FILE_NAME_LENGTH) == 0)
            {
                disk.clear();
                return Result<int>::Ok(entry.inodeIndex); // Found the file, return its inode index
            }
        }
    }
    return Result<int>::Err(BSError::make(
        ErrorCode::FILE_NOT_FOUND,
        "Error: File not found!")); // File not found in the directory
}

// traverses a directory path and returns the Inode Index of the final dir
Result<int> BlockStorageEngine::traversePath(vector<string> path)
{
    disk.clear();
    int curInodeIndex = 0;
    for (size_t i = 0; i < path.size(); i++)
    {

        Inode in = TRY(readInode(curInodeIndex), int);
        if (!in.isDirectory)
            return Result<int>::Err(BSError::make(
                ErrorCode::EXPECTED_A_DIRECTORY,
                "Error: Path is not a directory!"));
        for (int j = 0; j < in.blockCount; j++)
        {
            int blockIndex = in.directBlocks[j];
            bool found = false;
            for (long unsigned int k = 0; k < sb.blockSize / sizeof(DirectoryEntry); k++)
            {
                DirectoryEntry ent = TRY(readDirectoryEntry(k, blockIndex), int);
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
                return Result<int>::Err(BSError::make(
                    ErrorCode::FILE_NOT_FOUND,
                    "Error: File not found!"));
        }
    }
    return Result<int>::Ok(curInodeIndex);
}

// returns the index of the DirectoryEntry (index starting from the first block)
Result<int> BlockStorageEngine::findDirEntry(int dirInodeIndex, const char *fileName)
{
    int dirEntryCounter = -1;
    Inode in = TRY(readInode(dirInodeIndex), int);
    for (int i = 0; i < in.blockCount; i++)
    {
        for (long unsigned int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            dirEntryCounter++;
            DirectoryEntry ent = TRY(readDirectoryEntry(j, in.directBlocks[i]), int);
            if (strncmp(ent.fileName, fileName, DirectoryEntry::MAX_FILE_NAME_LENGTH) == 0)
            {
                return Result<int>::Ok(dirEntryCounter);
            }
        }
    }
    return Result<int>::Err(BSError::make(
        ErrorCode::DIRENT_NOT_FOUND,
        "Error: Directory Entry not found!"));
}

// PUBLIC METHODS

void BlockStorageEngine::printBitMap()
{
    disk.flush();
    disk.clear();

    cout << "Bitmap Status: " << endl;
    for (int i = 0; i < sb.blockCount; i++)
    {
        cout << (isBlockFree(i).isOk() ? "0" : "1") << ' ';
        if ((i + 1) % 64 == 0)
        {
            cout << endl; // New line after every 64 blocks for better readability
        }
    }
}

// CREATE / INITIATE / DISCONTINUE A DISK INSTANCE
Result<void> BlockStorageEngine::createDisk(const string &name, long sizeInMB)
{
    string baseDir = TRY(getAppDirectory(), void);
    fs::create_directories(baseDir);

    string fullPath = baseDir + name + SuperBlock::DISK_EXTENSION;
    if (findFileInDirectory(name, baseDir))
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DISK_ALREADY_EXISTS,
            "Error: Disk already exists at " + fullPath,
            "Suggestion: Use mountDisk() to mount an existing disk."));
    }
    {
        ofstream create(fullPath, ios::binary);
    }
    disk.open(fullPath, ios::in | ios::out | ios::binary | ios::trunc);

    if (!disk.is_open())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::CANNOT_CREATE_FILE,
            "Error: Could not create disk file at " + fullPath));
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
    TEST(writeInode(root, 0));
    sb.allocatedInodeCount++;
    syncSuperBlock();
    disk.flush();
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::mountDisk(const string &name)
{
    if (disk.is_open())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DISK_ALREADY_MOUNTED,
            "Error: Disk already mounted!",
            "Suggestion: unmountDisk() first."));
    }
    string baseDir = TRY(getAppDirectory(), void);
    if (!findFileInDirectory(name, baseDir))
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DISK_DOES_NOT_EXIST,
            "Error: Disk not created!",
            "Suggestion: createDisk() first."));
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
        disk.close();
        return Result<void>::Err(BSError::make(
            ErrorCode::DISK_CORRUPTED,
            "Error: Not a valid Block Storage Engine disk!"));
    }
    sb = tempSb;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::unmountDisk()
{
    updateSbInfo();
    syncSuperBlock();
    if (disk.is_open())
    {
        disk.close();
    }
    return Result<void>::Ok();
}

// CREATION OR DELETION OF FILES AND DIR'S
Result<void> BlockStorageEngine::save(const string &fileName, const string &filePath, int inodeIndex)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);
    string rfile = parsedPath[parsedPath.size() - 1];

    if (findInDirectory(rfile.c_str(), dirInodeIndex).isOk())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::FILE_ALREADY_EXISTS,
            "Error: " + rfile + " already exists. "));
    };

    // open the file to wanted to save
    fstream fileToSave;
    fileToSave.open(filePath, ios::in | ios::out | ios::binary);
    if (!fileToSave.is_open())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::CANNOT_CREATE_FILE,
            "Error: Could not open file at " + filePath));
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
    TEST(preSaveCheck(fileSize));

    // create a file Inode
    Inode fileInode;
    fileInode.fileSize = buffer.size();
    fileInode.isAllocated = true;
    fileInode.referenceCount = 1;

    // find and allocate free blocks for the fileInode based on the it's file size
    TEST(allocateBlock(fileInode, buffer));

    // update info saved in the disk
    syncSuperBlock();
    int freeInodeIndex = inodeIndex;
    if (inodeIndex == -1)
    {
        freeInodeIndex = TRY(findFreeInode(), void);
    }
    TEST(writeInode(fileInode, freeInodeIndex));
    sb.allocatedInodeCount++;

    // add the file entry to it's parent folder
    TEST(addDirectoryEntry(rfile.c_str(), dirInodeIndex, freeInodeIndex)); // Add entry to root directory
    cout << "File '" << rfile << "' saved successfully!" << endl;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::createDirectory(const string &path)
{
    disk.clear();
    vector<string> parsedPath = parseString(path, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);

    string lastElem = parsedPath[parsedPath.size() - 1];
    if (findInDirectory(lastElem.c_str(), dirInodeIndex).isOk())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::DIRECTORY_ALREADY_EXISTS,
            "Error: Directory already exists at " + path));
    };

    Inode dir;
    dir.isDirectory = true;
    dir.isAllocated = true;
    int freeInodeIndex = TRY(findFreeInode(), void);

    TEST(writeInode(dir, freeInodeIndex));
    TEST(addDirectoryEntry(lastElem.c_str(), dirInodeIndex, freeInodeIndex));
    sb.allocatedInodeCount++;
    disk.flush();
    cout << "Directory created at: " << path << endl;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::remove(const string &fileName)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);
    string file = parsedPath[parsedPath.size() - 1];

    int inodeIndex = TRY(findInDirectory(file.c_str(), dirInodeIndex), void);

    int dirEntryIndex = TRY(findDirEntry(dirInodeIndex, file.c_str()), void);

    freeDirectoryEntry(dirEntryIndex, dirInodeIndex);

    Inode in = TRY(readInode(inodeIndex), void);
    in.referenceCount--;
    if (in.referenceCount == 0)
    {
        for (int i = 0; i < in.blockCount; i++)
        {
            if (i < Inode::MAX_DIRECT_BLOCKS)
            {
                TEST(freeBlock(in.directBlocks[i]));
                in.directBlocks[i] = -1;
            }
        }
        if (in.indirectBlocks != -1)
        {
            int usedCount = in.blockCount - Inode::MAX_DIRECT_BLOCKS;
            TEST(freeIndirectBlocks(in.indirectBlocks, usedCount));
            TEST(freeBlock(in.indirectBlocks));
            in.indirectBlocks = -1;
        }
        in.blockCount = 0;
        in.indirectBlocks = -1;
        in.isAllocated = false;
        in.fileSize = 0;
        in.isDirectory = false;
    }
    TEST(writeInode(in, inodeIndex));
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::removeDirectory(const string &path)
{
    vector<string> parsedPath = parseString(path, '/');
    int dirInodeIndex = TRY(traversePath(parsedPath), void);
    string lastElem = parsedPath[parsedPath.size() - 1];

    Inode in = TRY(readInode(dirInodeIndex), void);
    if (!in.isDirectory)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::EXPECTED_A_DIRECTORY,
            "Error: Path is not a directory!"));
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        for (long unsigned int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent = TRY(readDirectoryEntry(j, in.directBlocks[i]), void);
            if (ent.isAllocated)
            {
                string entPathStr = path + "/" + ent.fileName;
                Inode entInode = TRY(readInode(ent.inodeIndex), void);

                if (entInode.isDirectory)
                    TEST(removeDirectory(entPathStr));
                else
                    TEST(remove(entPathStr));
                ent.isAllocated = false;
            }
        }
    }

    for (int i = 0; i < in.blockCount; i++)
    {
        TEST(freeBlock(in.directBlocks[i]));
        in.directBlocks[i] = -1;
    }

    in.lastBlockUsedBytes = 0;
    in.blockCount = 0;
    in.isAllocated = false;
    in.fileSize = 0;
    in.isDirectory = false;
    TEST(writeInode(in, dirInodeIndex));

    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int parentInodeIndex = TRY(traversePath(parentPath), void);

    int dirEntryIndex = TRY(findDirEntry(parentInodeIndex, lastElem.c_str()), void);
    freeDirectoryEntry(dirEntryIndex, parentInodeIndex);

    return Result<void>::Ok();
}

// RETRIEVAL OF FILES
Result<void> BlockStorageEngine::retrieve(const string &fileName, const string &destPath)
{
    vector<string> parsedPath = parseString(fileName, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);
    string rfile = parsedPath[parsedPath.size() - 1];

    int inodeIndex = TRY(findInDirectory(rfile.c_str(), dirInodeIndex), void);

    Inode recoveryInode = TRY(readInode(inodeIndex), void);

    vector<char> fileData = TRY(recoverFile(recoveryInode), void);
    fs::path outPath = fs::path(destPath) / rfile;
    ofstream output(outPath, ios::binary);
    if (!output.is_open())
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::CANNOT_CREATE_FILE,
            "Error: Could not create output file!",
            "Suggestion: Try using a different file."));
    }
    output.write(fileData.data(), fileData.size());
    output.close();
    return Result<void>::Ok();
}

// AUXILIARY FUNCTIONS

Result<void> BlockStorageEngine::link(const string &nfile, const string &efile)
{
    disk.clear();
    // efile: existing file, nfile: new file name
    vector<string> ePath = parseString(efile, '/');
    vector<string> e_parentPath(ePath.begin(), ePath.end() - 1);
    int e_dirInodeIndex = TRY(traversePath(e_parentPath), void);

    string efileName = ePath[ePath.size() - 1];
    int e_inodeIndex = TRY(findInDirectory(efileName.c_str(), e_dirInodeIndex), void);

    Inode e_inode = TRY(readInode(e_inodeIndex), void);
    e_inode.referenceCount++;
    TEST(writeInode(e_inode, e_inodeIndex));

    vector<string> nPath = parseString(nfile, '/');
    vector<string> n_parentPath(nPath.begin(), nPath.end() - 1);
    int n_dirInodeIndex = TRY(traversePath(n_parentPath), void);

    string nfileName = nPath[nPath.size() - 1];
    TEST(addDirectoryEntry(nfileName.c_str(), n_dirInodeIndex, e_inodeIndex));
    cout << "Link created between '" << efile << "' and '" << nfile << "'!" << endl;

    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::list(string path)
{
    vector<string> parsedPath = parseString(path, '/');
    int dirInodeIndex = TRY(traversePath(parsedPath), void);

    Inode in = TRY(readInode(dirInodeIndex), void);

    for (int i = 0; i < in.blockCount; i++)
    {
        for (long unsigned int j = 0; j < sb.blockSize / sizeof(DirectoryEntry); j++)
        {
            DirectoryEntry ent = TRY(readDirectoryEntry(j, in.directBlocks[i]), void);
            if (ent.isAllocated)
            {
                cout << ent.fileName << endl;
            }
        }
    }
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::move(const string &file, const string &dPath)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);

    int dirInodeIndex = TRY(traversePath(parentPath), void);
    string fileName = parsedPath[parsedPath.size() - 1];
    int dirEntryIndex = TRY(findDirEntry(dirInodeIndex, fileName.c_str()), void);

    int inodeIndex = TRY(findInDirectory(fileName.c_str(), dirInodeIndex), void);

    vector<string> dPathParsed = parseString(dPath, '/');
    vector<string> dParentPath(dPathParsed.begin(), dPathParsed.end());

    int dDirInodeIndex = TRY(traversePath(dParentPath), void);
    Inode dInode = TRY(readInode(dDirInodeIndex), void);

    TEST(addDirectoryEntry(fileName.c_str(), dDirInodeIndex, inodeIndex));

    TEST(freeDirectoryEntry(dirEntryIndex, dirInodeIndex));
    cout << "File moved from '" << file << "' to '" << dPath << "'!" << endl;

    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::rename(const string &file, const string &nName)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);
    string fileName = parsedPath[parsedPath.size() - 1];

    Inode in = TRY(readInode(dirInodeIndex), void);
    if (!in.isDirectory)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::EXPECTED_A_DIRECTORY,
            "Error: Path is not a directory!"));
    }

    int dirEntryIndex = TRY(findDirEntry(dirInodeIndex, fileName.c_str()), void);
    int dirEntryPerBlock = sb.blockSize / sizeof(DirectoryEntry);
    int dirEntOffset = dirEntryIndex % dirEntryPerBlock;
    int dirEntBlock = dirEntryIndex / dirEntryPerBlock;
    int blockIndex = in.directBlocks[dirEntBlock];

    DirectoryEntry ent = TRY(readDirectoryEntry(dirEntOffset, blockIndex), void);

    strncpy(ent.fileName, nName.c_str(), DirectoryEntry::MAX_FILE_NAME_LENGTH - 1);
    ent.isAllocated = true;
    ent.fileName[DirectoryEntry::MAX_FILE_NAME_LENGTH - 1] = '\0';
    TEST(writeDirEntry(ent, dirEntOffset, blockIndex));
    cout << "File renamed from '" << file << "' to '" << nName << "'!" << endl;
    return Result<void>::Ok();
}

Result<void> BlockStorageEngine::replace(const string &file, const string &newFilePath)
{
    vector<string> parsedPath = parseString(file, '/');
    vector<string> parentPath(parsedPath.begin(), parsedPath.end() - 1);
    int dirInodeIndex = TRY(traversePath(parentPath), void);

    string fileName = parsedPath[parsedPath.size() - 1];
    int inodeIndex = TRY(findInDirectory(fileName.c_str(), dirInodeIndex), void);

    Inode in = TRY(readInode(dirInodeIndex), void);
    if (!in.isDirectory)
    {
        return Result<void>::Err(BSError::make(
            ErrorCode::EXPECTED_A_DIRECTORY,
            "Error: Path is not a directory!"));
    }
    int dirEntryIndex = TRY(findDirEntry(dirInodeIndex, fileName.c_str()), void);

    vector<string> newPathParsed = parseString(newFilePath, '/');
    string newFileName = newPathParsed[newPathParsed.size() - 1];

    // save resets reference count to 0 so we have to save before and restore after save
    Inode oldIn = TRY(readInode(inodeIndex), void);
    int oldRefCount = oldIn.referenceCount;
    TEST(remove(file));
    parentPath.push_back(newFileName);
    string destPath = joinString(parentPath, '/');
    TEST(save(destPath, newFilePath, inodeIndex));

    // restore reference count
    Inode newIn = TRY(readInode(inodeIndex), void);
    newIn.referenceCount = oldRefCount;
    newIn.isAllocated = true;
    TEST(writeInode(newIn, inodeIndex));
    cout << "File replaced with '" << newFilePath << "'!" << endl;
    return Result<void>::Ok();
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
    return;
}

Result<void> BlockStorageEngine::printFileStructure(int dirInodeIndex, int depth)
{
    Inode in = TRY(readInode(dirInodeIndex), void);
    for (int j = 0; j < in.blockCount; j++)
    {
        size_t entriesInBlock = (j == in.blockCount - 1)
                                    ? in.lastBlockUsedBytes / sizeof(DirectoryEntry)
                                    : sb.blockSize / sizeof(DirectoryEntry);

        for (long unsigned int k = 0; k < entriesInBlock; k++)
        {
            DirectoryEntry ent = TRY(readDirectoryEntry(k, in.directBlocks[j]), void);
            if (!ent.isAllocated)
                continue;
            Inode in = TRY(readInode(ent.inodeIndex), void);
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
                cout << ent.fileName << endl;
                printFileStructure(ent.inodeIndex, depth + 1);
            }
        }
    }
    return Result<void>::Ok();
}

#undef TRY
#undef TEST
