# BlockStorageEngine (BSE)

A UNIX-like file system implemented from scratch in C++. BSE simulates a block-based disk with a superblock, bitmap, inode table, and data region — all stored in a single binary file on the host machine.

## Why am I building this?

This project exists because I'm trying to build something called ForgeDB.

Here's the problem I kept running into: game devs, embedded engineers, and tool builders
ship projects with hundreds of loose files, textures, configs, save data, assets. They're
scattered everywhere, hard to bundle, hard to version, and a pain to distribute. There's
no clean solution for just "put all my binary stuff in one portable file."

ForgeDB is my answer to that. It's a single-file embeddable key-value store where you
address your data using filesystem-style paths. You write a blob at `/assets/hero.png`,
read it back later, list everything under `/saves/`, delete entries. No SQL, no schema,
no server process running in the background. You link one library and you're done.

SQLite solved this for structured data. ForgeDB is trying to solve it for binary assets.

But ForgeDB needs a real filesystem underneath it to actually work, something that handles
blocks, inodes, directories, and disk layout properly. That's what block_storage_engine is.
I'm building the engine first so the thing on top of it is built on something solid.

One thing that genuinely excites me about ForgeDB: it'll ship with a terminal UI so you
can browse and manage your `.fdb` file using the same commands you already know. `ls`,
`cd`, `cat`. Your data, but navigable like a real directory tree, without extracting
anything.

That's where this is all going.

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

- **ForgeDB integration** — block_storage_engine currently gives you a real filesystem: blocks, inodes, directories. 
The next step is building ForgeDB on top of it, a single-file embeddable key-value store where you address binary data using filesystem-style paths (like /assets/hero.png or /saves/slot1). 
Without using any server, SQL or schema. You link one library, open a .fdb file, and read or write blobs. It is mildly similar with SQLite, but for raw binary assets instead of tables and rows. BSE is the engine under the hood; ForgeDB is what developers actually ship.
It will have a C-stle API, 

- **Directory inode scaling** — Right now each directory can hold roughly 384 entries before running out of space. That's fine for most use cases, but adding indirect block support would remove the ceiling entirely, the same way large file support works.

- **Crash recovery with WAL** — If the process dies mid-write, the disk can end up in a corrupt state. A Write-Ahead Log fixes this: every change is recorded to a log before it's applied, so after a crash the filesystem can replay or roll back cleanly. This is how production databases stay safe.
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

## Author

[Manasseh Samuel](https://github.com/etcoder-642)
Aspiring Systems Engineer.