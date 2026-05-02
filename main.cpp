#include <iostream>
#include <fstream>

using namespace std;

struct Account {
    int id;
    double balance;
};


// Changed return type to long long because file sizes can exceed double precision
long long fileSizeChecker(fstream &file) {
    file.clear(); // CRITICAL: Clear any error flags (like EOF)
    
    // Save current position
    streampos originalPos = file.tellg(); 
    
    file.seekg(0, ios::end);
    long long size = file.tellg();
    
    file.seekg(originalPos); // Return to where we were
    return size;
}

struct DiskMap {
    unsigned char bitmap[8];
};

bool isBlockFree(fstream &disk, int index) {
    disk.flush();
    disk.clear();
    int maskingIndex = index % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    short mask = 1 << maskingIndex;
    disk.seekg(index / 8, ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char*>(&bitnum), sizeof(unsigned char));
    disk.seekg(0, ios::beg); // Reset position after reading
    if(bitnum & mask){
        return false;
    }else return true;
}

void setBlockOccupied(fstream &disk, int index) {
    disk.flush();
    disk.clear();
    int maskingIndex = index % 8; // Determine which byte in the bitmap to check
    unsigned char bitnum;
    short mask = 1 << maskingIndex;
    disk.seekg(index / 8, ios::beg); // Move to the correct byte in the bitmap

    disk.read(reinterpret_cast<char*>(&bitnum), sizeof(unsigned char));
    disk.seekg(0, ios::beg); // Reset position after reading
    bitnum |= mask;
    disk.seekp(index/8, ios::beg);
    disk.write(reinterpret_cast<char*>(&bitnum), sizeof(unsigned char));
}

int main() {
    DiskMap myMainDisk = {{0,0,0,0,0,0,0,0}};
    fstream mainDisk("mainDisk.bin", ios::out | ios::in | ios::binary | ios::trunc);
    setBlockOccupied(mainDisk, 2);
    cout << "Bit Status at index 2: " << (isBlockFree(mainDisk, 2) ? "Free" : "Occupied") << endl;

    Account accounts[3];
    accounts[0] = {1, 40.0};
    accounts[1] = {2, 80.0};
    accounts[2] = {3, 125.0};

    fstream bankFile("bank.bin", ios::out | ios::in | ios::binary | ios::trunc);
    if(!bankFile) { cerr << "File open failed!"; return -1; }
    bankFile.write(reinterpret_cast<char*>(accounts), sizeof(accounts));
    Account readAccounts[3];
    bankFile.seekg(0, ios::beg);
    bankFile.read(reinterpret_cast<char*>(readAccounts), sizeof(readAccounts));
    cout << "\nInitial Account Data:" << endl;
    for(int i = 0; i < 3; i++) {
        cout << "Account ID: " << readAccounts[i].id << "     Balance: " << readAccounts[i].balance << endl;
    }
    cout << endl;
    bankFile.flush();
    bankFile.close();

    bankFile.open("bank.bin", ios::in | ios::binary | ios::out);
    if(!bankFile) { cerr << "File open failed!"; return -1; }

    bankFile.seekp(sizeof(Account) + offsetof(Account, balance), ios::beg);
    double newBalance = 999.99;
    bankFile.flush(); // Ensure all previous writes are flushed before seeking
    bankFile.write(reinterpret_cast<char*>(&newBalance), sizeof(double));
    bankFile.seekp(0, ios::cur); // Ensure we're at the correct position after seeking
    
    Account newAccounts[3];
    bankFile.clear(); // Clear flags before reading
    bankFile.flush(); // Ensure the updated balance is written to disk before reading
    bankFile.seekg(0, ios::beg);
    bankFile.read(reinterpret_cast<char*>(newAccounts), sizeof(newAccounts));
    bankFile.close();
    for(int i = 0; i < 3; i++) {
        cout << "Account ID: " << newAccounts[i].id << "     Balance: " << newAccounts[i].balance << endl;
    }
    cout << endl;

    double arr[5] = {1.1, 2.2, 3.3, 4.4, 5.5};

    // Use trunc to ensure we start with a fresh file for this test
    fstream arrFile("floats.bin", ios::out | ios::in | ios::binary | ios::trunc);

    if(!arrFile) { cerr << "File open failed!"; return 1; }

    arrFile.write(reinterpret_cast<const char*>(arr), sizeof(arr));
    
    // Force the data out of RAM and onto the disk
    arrFile.flush();

    cout << "File Size: " << fileSizeChecker(arrFile) << " bytes" << endl;

    double thirdValue = 0;
    arrFile.clear(); // Reset flags again before reading
    arrFile.seekg(2 * sizeof(double), ios::beg);
    
    if(arrFile.read(reinterpret_cast<char*>(&thirdValue), sizeof(double))) {
        cout << "Third Value (Index 2): " << thirdValue << endl;
    } else {
        cout << "Read failed!" << endl;
    }

    arrFile.close();
    return 0;
}
// #include <iostream>
// #include <vector>
// #include <string>
// #include <fstream>
// #include <array>

// using namespace std;

// struct Player {
//     int id;
//     float health;
//     short level;    
// };

// double fileSizeChecker(fstream &file) {
//     file.seekg(0, ios::end);
//     double size = file.tellg();
//     file.seekg(0, ios::beg);
//     return size;
// }

// int main() {
//     Player p1;
//     p1.id = 1;
//     p1.health = 95.5;
//     p1.level = 10;

//     ofstream outFile("playerData.bin", ios::binary);
//     outFile.write(reinterpret_cast<const char*>(&p1), sizeof(p1));
//     outFile.close();

//     ifstream inFile("playerData.bin", ios::binary);
//     Player p2;
//     inFile.read(reinterpret_cast<char*>(&p2), sizeof(p2));
//     inFile.close();

//     cout << "Player ID: " << p2.id << endl;
//     cout << "Player Health: " << p2.health << endl;
//     cout << "Player Level: " << p2.level << endl;


//     double arr[5] = {1.1, 2.2, 3.3, 4.4, 5.5};
//     fstream arrFile("floats.bin", ios::out | ios::in | ios::binary);

//     arrFile.write(reinterpret_cast<const char*>(arr), sizeof(arr));

//     cout << "File Size of floats.bin: " << fileSizeChecker(arrFile) << " bytes" << endl;
//     double thirdValue;
//     arrFile.seekg(2 * sizeof(double), ios::beg);
//     arrFile.read(reinterpret_cast<char*>(&thirdValue), sizeof(double));
//     cout << "Third Value: " << thirdValue << endl;
//     arrFile.close();
//     return 0;
// }