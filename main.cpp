#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#include "api.h"

using namespace std;

int main()
{
    BlockStorageEngine drive;
    drive.mountDisk("drive");


    drive.retrieve("test1.jpg", "./assets/recovered/");
    drive.retrieve("test_img.jpg", "./assets/recovered/");
    drive.retrieve("something.txt", "./assets/recovered/");
    drive.retrieve("dump.txt", "./assets/recovered/");

    return 0;
}