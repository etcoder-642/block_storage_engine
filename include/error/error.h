#ifndef ERROR_H
#define ERROR_H

#include <iostream>
#include <string>
#include <vector>
#include <source_location>

namespace bse {
    enum class ErrorCode {
        // filesystem errors
        CANNOT_CREATE_FILE,
        HOME_ENVIRONMENT_VARIABLE_NOT_FOUND,

        // disk errors
        DISK_FULL,
        DISK_NOT_MOUNTED,
        DISK_CORRUPTED,
        DISK_ALREADY_EXISTS,

        // block errors
        BLOCK_NOT_FOUND,
        BLOCK_ALREADY_OCCUPIED,
        INVALID_BLOCK_INDEX,
        BLOCK_ALLOCATION_ERROR, // Used when allocating a block for a file that is a directory

        // Inode errors
        INODE_NOT_FOUND,
        INODE_ALREADY_EXISTS,
        INODE_TABLE_FULL,
        INVALID_INODE_INDEX,

        // dirEntry errors
        DIRENT_NOT_FOUND,
        DIRENT_ALREADY_EXISTS,
        DIRENT_ALREADY_ALLOCATED,
        INVALID_DIR_ENTRY_OFFSET,

        // file errors
        FILE_NOT_FOUND,
        FILE_ALREADY_EXISTS,
        EXCEEDS_MAX_FILE_SIZE,
        FILE_TOO_LARGE,
        EXPECTED_A_DIRECTORY,

        // directory errors
        NOT_A_DIRECTORY,
        DIRECTORY_ALREADY_EXISTS,
        EXPECTED_A_FILE,
        DIRECTORY_FULL,

        // path errors
        INVALID_PATH,

        // syntax errors
        INVALID_FILE_NAME,
        INVALID_FILE_TYPE,

        UNKNOWN
    };

    struct StackFrame {
        std::string function;
        std::string file;
        uint32_t line;
    };

    struct BSError {
        ErrorCode code;
        std::string message;
        std::string suggestion;

        std::vector<StackFrame> stackTrace;
        std::string context;

        BSError(ErrorCode code, std::string message, std::string suggestion = "", std::string context = "") : code(code), message(message), suggestion(suggestion) {}

        void pushFrame(const std::source_location &loc) {
            stackTrace.push_back(StackFrame{
                loc.function_name(),
                loc.file_name(),
                loc.line()
            });
        }
    };
}


#endif