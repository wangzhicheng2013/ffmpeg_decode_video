#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <iostream>
#include <thread>
#include <fstream>
#include "single_instance.hpp"
enum FILE_WRITE_MODE {
    OVER_WRITE,
    APPEND_WRITE
};
class file_utility {
public:
    bool write_file_content(const char *path, const char *buf, size_t size, unsigned mode = OVER_WRITE) {
        if (!path || !buf) {
            return false;
        }
        std::ofstream  ofs;
        switch (mode)
        {
        case OVER_WRITE:
            ofs.open(path, std::ios::in | std::ios::trunc);
            if (!ofs.is_open()) {
                return false;
            }
            break;
        case APPEND_WRITE:
            ofs.open(path, std::ios::in | std::ios::app);
            if (!ofs.is_open()) {
                return false;
            }
            break;
        default:
            return false;
            break;
        }
        ofs.write(buf, size);
        ofs.close();
        return true;
    }
};

#define  G_FILE_UTILITY single_instance<file_utility>::instance()