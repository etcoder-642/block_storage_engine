#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#include "../include/api.h"

using namespace std;

int main()
{
    // TESTING
    BlockStorageEngine drive;
    // drive.createDisk("test", 16);

    // drive.createDirectory("home");
    // drive.createDirectory("home/test");
    // drive.createDirectory("home/bin");
    // drive.createDirectory("home/test/test2");

    // drive.save("home/test100kb.txt", "./assets/test100kb.txt");
    // drive.save("home/test/test1mb.txt", "./assets/test1mb.txt");
    // drive.save("home/test/test2/test2mb.txt", "./assets/test2mb.txt");
    // drive.save("home/bin/test500kb.txt", "./assets/test500kb.txt");

    // drive.link("home/bin/test1mb.txt", "home/test/test1mb.txt");
    // // drive.link("home/test/test500kb.txt", "home/bin/test500kb.txt");

    // cout << "Home Directory Contents:" << endl;
    // drive.list("home");
    // cout << "Home/Test Directory Contents:" << endl;
    // drive.list("home/test");
    // cout << "Home/bin Directory Contents:" << endl;
    // drive.list("home/bin");

    // cout << "File Structure:" << endl;
    // drive.printFileStructure();

    // drive.rename("home/test/test1mb.txt", "test1mb-renamed.txt");
    // drive.move("home/test/test1mb-renamed.txt", "home/test/test2");

    // drive.replace("home/test/test2/test2mb.txt", "./assets/test_3mb.txt");

    // // drive.diskInfo();
    // drive.printFileStructure();

    // // retrieve files
    // drive.retrieve("home/test100kb.txt", "./assets/recovered/");
    // drive.retrieve("home/test/test2/test1mb-renamed.txt", "./assets/recovered/");
    // drive.retrieve("home/test/test2/test_3mb.txt", "./assets/recovered/");

    // drive.remove("home/bin/test500kb.txt");

    // drive.printFileStructure();
    // // drive.printBitMap();
    // drive.unmountDisk();

    // testing after unmounting
    // drive.mountDisk("test");
    // drive.printFileStructure();

    // drive.createDirectory("home/bin/test3");
    // drive.createDirectory("home/bin/test3/test4");
    // drive.move("home/bin/test1mb.txt", "home/bin/test3");
    // drive.link("home/test/test1mb.txt", "home/bin/test3/test1mb.txt");
    // drive.link("home/bin/test3/test_3mb-linked.txt", "home/test/test2/test_3mb.txt");
    // drive.link("home/bin/test3/test4/test_3mb-linked.txt", "home/test/test2/test_3mb.txt");
    // drive.printFileStructure();

    // drive.removeDirectory("home/bin/test3");
    // drive.printFileStructure();
    // drive.printBitMap();
    // drive.unmountDisk();
    return 0;
}   