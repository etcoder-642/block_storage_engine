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
    BlockStorageEngine drive;
    drive.mountDisk("drive");
    // drive.printBitMap();
    // drive.createDisk("drive", 8);


    drive.retrieve("test1.jpg", "./assets/recovered/");
    drive.retrieve("test_img.jpg", "./assets/recovered/");
    drive.retrieve("something.txt", "./assets/recovered/");
    drive.retrieve("dump.txt", "./assets/recovered/");

    return 0;
}