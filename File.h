#pragma once

#include <stdint.h>
#include <vector>

class FileDescriptorRO {
  public:
    int fd;

    FileDescriptorRO(const char *filename);
    ~FileDescriptorRO();
};

std::vector<uint8_t> getBuffer(int fd);
