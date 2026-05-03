#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#include "api.h"

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

int main()
{
    // DiskMap myMainDisk = {{0, 0, 0, 0, 0, 0, 0, 0}};
    // SuperBlock mySuperBlock;
    // Inode myInode[mySuperBlock.inodeCount];
    // fstream mainDisk("./drive/mainDisk.bin", ios::out | ios::in | ios::binary | ios::trunc);
    // formatDisk(mainDisk, mySuperBlock);
    // mainDisk.write(reinterpret_cast<char *>(&mySuperBlock), sizeof(SuperBlock));
    // mainDisk.write(reinterpret_cast<char *>(&myMainDisk), sizeof(DiskMap));
    // setBlockOccupied(mainDisk, mySuperBlock, 2); // Mark block 2 as occupied

    // cout << "Bit Status at index 2: " << (isBlockFree(mainDisk, mySuperBlock, 2) ? "Free" : "Occupied") << endl;
    // cout << "Disk Size: " << fileSizeChecker(mainDisk) << " bytes" << endl;
    // cout << "Disk Size(KB): " << fileSizeChecker(mainDisk) / 1024.0 << " KB" << endl;
    // cout << "Disk Size(MB): " << fileSizeChecker(mainDisk) / 1048576.0 << " MB" << endl;

    // vector<char> testData = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
    // Inode testInode;
    // testInode.fileSize = testData.size();
    // allocateBlock(mainDisk, mySuperBlock, testInode, testData);
    // writeInode(mainDisk, testInode, mySuperBlock, 0);
    // cout << "Initial Test File: " << testData.data() << endl;

    // vector<char> recoveredData = recoverFile(mainDisk, mySuperBlock, testInode);
    // cout << "Recovered Test File: " << recoveredData.data() << endl;

    // // realworld test
    // ifstream realImgFile("./assets/test_img.jpg", ios::binary);
    // if (!realImgFile)    {
    //     cerr << "Image file open failed!" << endl;
    //     return -1;
    // }
    // realImgFile.seekg(0, ios::end);
    // long imgSize = realImgFile.tellg();
    // realImgFile.seekg(0, ios::beg);
    // vector<char> imgData(imgSize);
    // realImgFile.read(imgData.data(), imgSize);
    // realImgFile.close();

    // Inode imgInode;
    // imgInode.fileSize = imgData.size();
    // allocateBlock(mainDisk, mySuperBlock, imgInode, imgData);
    // writeInode(mainDisk, imgInode, mySuperBlock, 1);

    // Inode recoveredInode = readInode(mainDisk, mySuperBlock, 1);
    // vector<char> recoveredImgData = recoverFile(mainDisk, mySuperBlock, recoveredInode);
    // string outPath = "./assets/recovered_img.jpg";
    // ofstream output(outPath, ios::binary);
    // output.write(recoveredImgData.data(), recoveredImgData.size());
    // output.close();


    // // print bitmap before writing the image
    // printBitMap(mainDisk, mySuperBlock);
    // return 0;
}