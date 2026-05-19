#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include "../include/api.h"

using namespace std;
namespace fs = std::filesystem;

// ─── helpers ───────────────────────────────────────────────────────────────

void section(const string& title) {
    cout << "\n══════════════════════════════════════\n";
    cout << "  " << title << "\n";
    cout << "══════════════════════════════════════\n";
}

void check(const string& label, bse::Result<void> r) {
    cout << (r.isOk() ? "[PASS] " : "[FAIL] ") << label;
    if (r.isErr()) {
        cout << "\n       ↳ " << r.getErrMessage();
        r.printStackTrace();  // <-- add this
    }
    cout << "\n";
}

// ensure test assets exist
void createAssets() {
    fs::create_directories("./assets/recovered");

    auto makeFile = [](const string& path, size_t bytes) {
        if (fs::exists(path)) return;
        ofstream f(path, ios::binary);
        vector<char> buf(bytes, 'A');
        f.write(buf.data(), bytes);
    };

    makeFile("./assets/test100kb.txt",    100 * 1024);
    makeFile("./assets/test500kb.txt",    500 * 1024);
    makeFile("./assets/test1mb.txt",    1 * 1024 * 1024);
    makeFile("./assets/test2mb.txt",    2 * 1024 * 1024);
    makeFile("./assets/test_3mb.txt",   3 * 1024 * 1024);
}

// ─── test groups ───────────────────────────────────────────────────────────

void testDiskLifecycle(bse::BlockStorageEngine& drive) {
    section("DISK LIFECYCLE");

    check("createDisk (auto-mounts)",                       drive.createDisk("test", 16));

    // error cases
    check("createDisk duplicate [expect FAIL]",             drive.createDisk("test", 16));
    check("mountDisk while already mounted [expect FAIL]",  drive.mountDisk("test"));
}


void testDirectories(bse::BlockStorageEngine& drive) {
    section("DIRECTORIES");

    check("createDirectory home",              drive.createDirectory("home"));
    check("createDirectory home/docs",         drive.createDirectory("home/docs"));
    check("createDirectory home/bin",          drive.createDirectory("home/bin"));
    check("createDirectory home/docs/nested",  drive.createDirectory("home/docs/nested"));

    // error cases
    check("createDirectory duplicate [expect FAIL]",      drive.createDirectory("home"));
    check("createDirectory invalid path [expect FAIL]",   drive.createDirectory("nonexistent/dir"));
}

void testSave(bse::BlockStorageEngine& drive) {
    section("SAVE");

    check("save 100kb",  drive.save("home/test100kb.txt",              "./assets/test100kb.txt"));
    check("save 1mb",    drive.save("home/docs/test1mb.txt",           "./assets/test1mb.txt"));
    check("save 2mb",    drive.save("home/docs/nested/test2mb.txt",    "./assets/test2mb.txt"));
    check("save 500kb",  drive.save("home/bin/test500kb.txt",          "./assets/test500kb.txt"));

    // error cases
    check("save to nonexistent dir [expect FAIL]",  drive.save("ghost/file.txt", "./assets/test100kb.txt"));
    check("save nonexistent file [expect FAIL]",    drive.save("home/ghost.txt", "./assets/ghost.txt"));
    check("save duplicate [expect FAIL]",           drive.save("home/test100kb.txt", "./assets/test100kb.txt"));
}

void testLinks(bse::BlockStorageEngine& drive) {
    section("LINKS");

    check("link 1mb into bin",   drive.link("home/bin/test1mb.txt", "home/docs/test1mb.txt"));

    // error cases
    check("link nonexistent source [expect FAIL]",  drive.link("home/bin/ghost.txt", "home/ghost.txt"));
    check("link duplicate [expect FAIL]",           drive.link("home/bin/test1mb.txt", "home/docs/test1mb.txt"));
}

void testListAndStructure(bse::BlockStorageEngine& drive) {
    section("LIST & STRUCTURE");

    cout << "\n-- list home --\n";          drive.list("home");
    cout << "\n-- list home/docs --\n";     drive.list("home/docs");
    cout << "\n-- list home/bin --\n";      drive.list("home/bin");
    cout << "\n-- printFileStructure --\n"; drive.printFileStructure();

    // error case
    check("list nonexistent [expect FAIL]", drive.list("home/ghost"));
}

void testRenameAndMove(bse::BlockStorageEngine& drive) {
    section("RENAME & MOVE");

    check("rename 1mb",                    drive.rename("home/docs/test1mb.txt", "test1mb-renamed.txt"));
    check("move renamed into nested",      drive.move("home/docs/test1mb-renamed.txt", "home/docs/nested"));

    // error cases
    check("rename nonexistent [expect FAIL]",       drive.rename("home/docs/ghost.txt", "new.txt"));
    check("move to nonexistent dir [expect FAIL]",  drive.move("home/test100kb.txt", "home/ghost"));
}

void testReplace(bse::BlockStorageEngine& drive) {
    section("REPLACE");

    check("replace 2mb with 3mb",  drive.replace("home/docs/nested/test2mb.txt", "./assets/test_3mb.txt"));

    // error case
    check("replace nonexistent [expect FAIL]", drive.replace("home/ghost.txt", "./assets/test100kb.txt"));
}

void testRetrieve(bse::BlockStorageEngine& drive) {
    section("RETRIEVE");

    check("retrieve 100kb",         drive.retrieve("home/test100kb.txt",                    "./assets/recovered/"));
    check("retrieve renamed 1mb",   drive.retrieve("home/docs/nested/test1mb-renamed.txt",  "./assets/recovered/"));
    check("retrieve replaced 3mb",  drive.retrieve("home/docs/nested/test_3mb.txt",         "./assets/recovered/"));

    // error case
    check("retrieve nonexistent [expect FAIL]", drive.retrieve("home/ghost.txt", "./assets/recovered/"));
}

void testRemove(bse::BlockStorageEngine& drive) {
    section("REMOVE");

    check("remove 500kb",  drive.remove("home/bin/test500kb.txt"));

    // error cases
    check("remove nonexistent [expect FAIL]",  drive.remove("home/bin/ghost.txt"));
    check("remove directory as file [expect FAIL]", drive.remove("home/bin"));

    cout << "\n-- structure after remove --\n";
    drive.printFileStructure();
}

void testRemoveDirectory(bse::BlockStorageEngine& drive) {
    section("REMOVE DIRECTORY");

    drive.createDirectory("home/bin/trash");
    drive.createDirectory("home/bin/trash/sub");
    drive.save("home/bin/trash/file.txt", "./assets/test100kb.txt");
    drive.link("home/bin/trash/sub/linked.txt", "home/bin/trash/file.txt");

    check("removeDirectory recursive",  drive.removeDirectory("home/bin/trash"));

    // error case
    check("removeDirectory nonexistent [expect FAIL]", drive.removeDirectory("home/ghost"));

    cout << "\n-- structure after removeDirectory --\n";
    drive.printFileStructure();
}

void testPersistence(bse::BlockStorageEngine& drive) {
    section("PERSISTENCE (unmount → remount)");

    check("unmount", drive.unmountDisk());
    check("remount", drive.mountDisk("test"));

    cout << "\n-- structure after remount --\n";
    drive.printFileStructure();

    check("unmount [expect FAIL on double]", drive.unmountDisk());
    check("unmount final", drive.unmountDisk());
}

// ─── main ──────────────────────────────────────────────────────────────────

int main() {
    createAssets();

    bse::BlockStorageEngine drive;

    testDiskLifecycle(drive);
    testDirectories(drive);
    testSave(drive);
    testLinks(drive);
    testListAndStructure(drive);
    testRenameAndMove(drive);
    testReplace(drive);
    testRetrieve(drive);
    testRemove(drive);
    testRemoveDirectory(drive);
    testPersistence(drive);

    drive.diskInfo();
    return 0;
}