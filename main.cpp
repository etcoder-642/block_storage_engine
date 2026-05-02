#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

using namespace std;


// Changed return type to long long because file sizes can exceed double precision
long long fileSizeChecker(fstream &file)
{
    file.clear(); // CRITICAL: Clear any error flags (like EOF)

    // Save current position
    streampos originalPos = file.tellg();

    file.seekg(0, ios::end);
    long long size = file.tellg();

    file.seekg(originalPos); // Return to where we were
    return size;
}

struct DiskMap
{
    unsigned char bitmap[8];
};

struct SuperBlock
{
    int magicNumber = 12345;
    int blockSize = 512;
    int blockCount = 1024;

    int bitmapSize = 1024 / 8;
    int inodeCount = 64;
    int inodeSize = 64; // Size of each inode in bytes

    int bitmapStart = sizeof(SuperBlock); // Bitmap starts immediately after the SuperBlock
    int inodeTableStart = bitmapStart + bitmapSize;
    int dataRegionStart = inodeTableStart + (inodeSize * inodeCount);
};

struct Inode
{
    int fileSize = 0;
    int blockCount = 0;
    int directBlocks[12] = {-1};

    bool isDirectory = false;
    char padding[7] = {0}; // Padding to ensure the struct is exactly 64 bytes
};
static_assert(sizeof(Inode) == 64, "Inode size mismatch");

bool isBlockFree(fstream &disk, SuperBlock &sb, int index)
{
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

void setBlockOccupied(fstream &disk, SuperBlock &sb, int index)
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

long calculateTotalSize(SuperBlock &sb)
{
    long totalSize = sb.blockSize * sb.blockCount; // Size of all blocks
    totalSize += sb.bitmapSize;                    // Add bitmap size
    totalSize += sb.inodeSize * sb.inodeCount;     // Add size of all inodes
    return totalSize;
}

void formatDisk(fstream &disk, SuperBlock &sb)
{
    disk.seekp(0, ios::beg);
    char zero = 0;
    for (long i = 0; i < calculateTotalSize(sb); i++)
    {
        disk.write(&zero, 1);
    }
}

int findFreeBlock(fstream &disk, SuperBlock &sb) {
    int index = 0;

    // "Skip" all blocks that are already taken
    while (!isBlockFree(disk, sb, index)) {
        index++;
        if (index >= sb.blockCount) {
            return -1; // Standard way to signal "Disk Full"
        }
    }

    // Now index is pointing to a free block!
    setBlockOccupied(disk, sb, index);
    return index;
}


void allocateBlock(fstream &disk, SuperBlock &sb, Inode &in, vector<char> &data)
{
    disk.flush();
    disk.clear();

    char *dataPtr = data.data();
    int bytesRemaining = data.size();
    long writeLocation = 0;
    int blockSize = sb.blockSize;
    while (bytesRemaining > 0)
    {
        int amountToWrite = (blockSize < bytesRemaining) ? blockSize : bytesRemaining;

        int freeBlockID = findFreeBlock(disk, sb);

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

vector<char> recoverFile(fstream &disk, SuperBlock &sb, Inode &in)
{
    disk.flush();
    disk.clear();

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

void writeInode(fstream &disk, Inode &in, SuperBlock &sb, int inodeIndex)
{
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekp(pos, ios::beg);
    disk.write(reinterpret_cast<char*>(&in), sb.inodeSize);
    disk.flush();
}

Inode readInode(fstream &disk, SuperBlock &sb, int inodeIndex)
{
    Inode myInode;
    long pos = sb.inodeTableStart + (inodeIndex * sb.inodeSize);
    disk.seekg(pos, ios::beg);
    disk.read(reinterpret_cast<char*>(&myInode), sb.inodeSize);
    disk.clear();
    return myInode;
}

void printBitMap(fstream &disk, SuperBlock &sb) {
    disk.flush();
    disk.clear();

    cout << "Bitmap Status: " << endl;
    for (int i = 0; i < sb.blockCount; i++) {
        cout << (isBlockFree(disk, sb, i) ? "0" : "1") << ' ';
        if((i + 1) % 64 == 0) {
            cout << endl; // New line after every 64 blocks for better readability
        }
    }
}

int main()
{
    DiskMap myMainDisk = {{0, 0, 0, 0, 0, 0, 0, 0}};
    SuperBlock mySuperBlock;
    Inode myInode[mySuperBlock.inodeCount];
    fstream mainDisk("mainDisk.bin", ios::out | ios::in | ios::binary | ios::trunc);
    formatDisk(mainDisk, mySuperBlock);
    mainDisk.write(reinterpret_cast<char *>(&mySuperBlock), sizeof(SuperBlock));
    mainDisk.write(reinterpret_cast<char *>(&myMainDisk), sizeof(DiskMap));
    setBlockOccupied(mainDisk, mySuperBlock, 2); // Mark block 2 as occupied

    cout << "Bit Status at index 2: " << (isBlockFree(mainDisk, mySuperBlock, 2) ? "Free" : "Occupied") << endl;
    cout << "Disk Size: " << fileSizeChecker(mainDisk) << " bytes" << endl;
    cout << "Disk Size(KB): " << fileSizeChecker(mainDisk) / 1024.0 << " KB" << endl;
    cout << "Disk Size(MB): " << fileSizeChecker(mainDisk) / 1048576.0 << " MB" << endl;

    vector<char> testData = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
    Inode testInode;
    testInode.fileSize = testData.size();
    allocateBlock(mainDisk, mySuperBlock, testInode, testData);
    writeInode(mainDisk, testInode, mySuperBlock, 0);
    cout << "Initial Test File: " << testData.data() << endl;

    vector<char> recoveredData = recoverFile(mainDisk, mySuperBlock, testInode);
    cout << "Recovered Test File: " << recoveredData.data() << endl;

    // realworld test
    ifstream realImgFile("test_img.jpg", ios::binary);
    if (!realImgFile)    {
        cerr << "Image file open failed!" << endl;
        return -1;
    }
    realImgFile.seekg(0, ios::end);
    long imgSize = realImgFile.tellg();
    realImgFile.seekg(0, ios::beg);
    vector<char> imgData(imgSize);
    realImgFile.read(imgData.data(), imgSize);
    realImgFile.close();

    Inode imgInode;
    imgInode.fileSize = imgData.size();
    allocateBlock(mainDisk, mySuperBlock, imgInode, imgData);
    writeInode(mainDisk, imgInode, mySuperBlock, 1);

    Inode recoveredInode = readInode(mainDisk, mySuperBlock, 1);
    vector<char> recoveredImgData = recoverFile(mainDisk, mySuperBlock, recoveredInode);
    string outPath = "recovered_img.jpg";
    ofstream output(outPath, ios::binary);
    output.write(recoveredImgData.data(), recoveredImgData.size());
    output.close();


    // print bitmap before writing the image
    printBitMap(mainDisk, mySuperBlock);
    return 0;
}