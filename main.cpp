#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#include "api.h"

using namespace std;

int main()
{
    BlockStorageEngine drive;
    drive.mountDisk("test");

    // drive.save("test1mb.txt", "./assets/test1mb.txt");
    drive.retrieve("test1mb.txt", "./assets/recovered");

    return 0;
}