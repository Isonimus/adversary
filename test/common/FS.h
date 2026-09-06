#pragma once

#ifndef ESP32

#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <stdarg.h>

#define FILE_WRITE "w"
#define FILE_APPEND "a"
#define FILE_READ "r"

class File {
public:
    File() : m_file(nullptr) {}
    File(FILE* f) : m_file(f) {}
    
    operator bool() const { return m_file != nullptr; }
    
    size_t write(const uint8_t *buf, size_t size) { 
        if (!m_file) return 0;
        return fwrite(buf, 1, size, m_file); 
    }
    
    void print(const char* s) { if (m_file) fprintf(m_file, "%s", s); }
    void print(const std::string& s) { if (m_file) fprintf(m_file, "%s", s.c_str()); }
    
    void println(const char* s = "") { if (m_file) fprintf(m_file, "%s\n", s); }
    void println(const std::string& s) { if (m_file) fprintf(m_file, "%s\n", s.c_str()); }
    
    void printf(const char* fmt, ...) {
        if (!m_file) return;
        va_list args;
        va_start(args, fmt);
        vfprintf(m_file, fmt, args);
        va_end(args);
    }
    
    void flush() {
        if (m_file) fflush(m_file);
    }
    
    size_t write(uint8_t c) {
        if (!m_file) return 0;
        return fwrite(&c, 1, 1, m_file);
    }

    int read() {
        if (!m_file) return -1;
        return fgetc(m_file);
    }

    size_t read(uint8_t* buf, size_t size) {
        if (!m_file) return 0;
        return fread(buf, 1, size, m_file);
    }

    int peek() {
        if (!m_file) return -1;
        int c = fgetc(m_file);
        if (c != EOF) ungetc(c, m_file);
        return c;
    }

    // Random access (matches ESP32 FS::File seek/position semantics).
    bool seek(uint32_t pos) {
        if (!m_file) return false;
        return fseek(m_file, (long)pos, SEEK_SET) == 0;
    }

    size_t position() {
        if (!m_file) return 0;
        long pos = ftell(m_file);
        return pos < 0 ? 0 : (size_t)pos;
    }

    int available() {
        if (!m_file) return 0;
        long pos = ftell(m_file);
        fseek(m_file, 0, SEEK_END);
        long end = ftell(m_file);
        fseek(m_file, pos, SEEK_SET);
        return (int)(end - pos);
    }

    size_t size() {
        if (!m_file) return 0;
        long pos = ftell(m_file);
        fseek(m_file, 0, SEEK_END);
        long sz = ftell(m_file);
        fseek(m_file, pos, SEEK_SET);
        return (size_t)sz;
    }

    bool isDirectory() { return false; } // simplified

    void close() {
        if (m_file) {
            fclose(m_file);
            m_file = nullptr;
        }
    }

private:
    FILE* m_file;
};

#endif
