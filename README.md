# BlockStorageEngine (BSE)

A UNIX-like file system implemented from scratch in C++. BSE simulates a block-based disk with a superblock, bitmap, inode table, and data region — all stored in a single binary file on the host machine.

---

## Architecture

```
[ SuperBlock | Bitmap | Inode Table | Data Region ]
```

| Region | Purpose |
|---|---|
| SuperBlock | Disk metadata: block count, inode count, region offsets |
| Bitmap | One bit per block tracking free/occupied state |
| Inode Table | Fixed-size array of 128-byte inodes |
| Data Region | Raw block storage for file and directory data |

### Key structures

**Inode (128 bytes)**
- 24 direct block pointers
- 1 indirect block pointer (extends capacity by up to 1024 blocks)
- File size, block count, reference count, allocation/directory flags

**DirectoryEntry (64 bytes)**
- 59-byte filename
- Inode index pointer
- Allocation flag

**SuperBlock**
- Magic number (`0x406EDB`) for disk validation on mount
- Block size: 4096 bytes
- Dynamic inode count: 5% of total disk size reserved for inodes

---

## Disk Layout on Host

Disks are stored as `.fdb` binary files:

| OS | Path |
|---|---|
| Linux | `~/.local/share/BlockEngine/<name>.fdb` |
| macOS | `~/Library/Application Support/BlockEngine/<name>.fdb` |
| Windows | `%APPDATA%\BlockEngine\<name>.fdb` |

---

## API

### Disk lifecycle

```cpp
drive.createDisk("mydisk", 16);   // create a 16MB disk
drive.mountDisk("mydisk");        // mount existing disk
drive.unmountDisk();              // flush and close
```

### Files

```cpp
drive.save("home/file.txt", "./local/file.txt");     // save from host path
drive.retrieve("home/file.txt", "./output/");        // recover to host path
drive.remove("home/file.txt");                       // delete file
drive.replace("home/file.txt", "./new_file.txt");    // replace contents in-place
```

### Directories

```cpp
drive.createDirectory("home/docs");        // create directory (path must exist up to last segment)
drive.removeDirectory("home/docs");        // recursively remove directory and all contents
```

### File operations

```cpp
drive.rename("home/old.txt", "new.txt");                   // rename in place
drive.move("home/file.txt", "home/docs");                  // move to directory
drive.link("home/docs/alias.txt", "home/file.txt");        // create hard link
```

### Inspection

```cpp
drive.list("home");               // list directory contents
drive.printFileStructure();       // recursive tree from root
drive.diskInfo();                 // superblock stats
drive.printBitMap();              // raw bitmap state
```

---

## Features

- **Hard links** — multiple directory entries pointing to the same inode; reference counted
- **Indirect blocks** — files larger than 24 × 4096 bytes (96KB) automatically use an indirect block, extending max file size to ~4MB
- **Persistent storage** — disk state survives unmount/remount cycles
- **Dynamic disk sizing** — inode count and block count computed at creation time from disk size
- **Path traversal** — full hierarchical path support (`home/docs/subdir/file.txt`)

---

## Things to work on in the near future

- Directory inodes do not support indirect blocks (max ~384 entries per directory)
- Single level of indirection (no double/triple indirect blocks)
- No permissions, timestamps, or user/group metadata
- No journaling or crash recovery

---

## Building

**Requirements:** g++ with C++17 support

```bash
git clone https://github.com/etcoder-642/block_storage_engine.git
cd block_storage_engine
make
./main
```

To clean the build:
```bash
make clean
```

---
## File extension

`.fdb` — ForgeDB disk image