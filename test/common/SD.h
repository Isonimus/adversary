#pragma once

#ifndef ESP32

#include "FS.h"
#include <sys/stat.h>
#include <unistd.h>

class SDClass {
public:
    bool begin(uint8_t ssPin = -1) { return true; }
    
    bool exists(const char* path) {
        const char* p = (path && path[0] == '/') ? path + 1 : path;
        return access(p, F_OK) != -1;
    }
    
    bool mkdir(const char* path) {
        const char* p = (path && path[0] == '/') ? path + 1 : path;
        // Basic host-side directory creation for tests
#ifdef _WIN32
        return _mkdir(p) == 0 || errno == EEXIST;
#else
        return ::mkdir(p, 0777) == 0 || errno == EEXIST;
#endif
    }
    
    File open(const char* path, const char* mode = FILE_READ) {
        const char* p = (path && path[0] == '/') ? path + 1 : path;
        FILE* f = fopen(p, mode);
        return File(f);
    }
    
    bool remove(const char* path) {
        const char* p = (path && path[0] == '/') ? path + 1 : path;
        return ::remove(p) == 0;
    }

    bool rmdir(const char* path) {
        const char* p = (path && path[0] == '/') ? path + 1 : path;
        return ::rmdir(p) == 0;
    }

    bool rename(const char* oldPath, const char* newPath) {
        const char* op = (oldPath && oldPath[0] == '/') ? oldPath + 1 : oldPath;
        const char* np = (newPath && newPath[0] == '/') ? newPath + 1 : newPath;
        return ::rename(op, np) == 0;
    }
};

extern SDClass SD;

#endif
