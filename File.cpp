
#include "File.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/stat.h>

#include <vector>

FileDescriptorRO::FileDescriptorRO(const char *filename) {
    fd = open(filename, O_RDONLY);

    if (fd == -1) {
        printf("Cannot open file for reading\n");
        throw -1;
    }
}

FileDescriptorRO::~FileDescriptorRO() { close(fd); }

std::vector<uint8_t> getBuffer(int fd) {
    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0) {
        printf("Cannot open file\n");
        throw -1;
    }

    std::vector<uint8_t> buf(statbuf.st_size);

    const ssize_t hasRead = read(fd, buf.data(), buf.size());

    if (hasRead == -1) {
        printf("Cannot read from file\n");
        throw -1;
    }

    if ((size_t)hasRead != buf.size()) {
        printf("Short read from file\n");
        throw -1;
    }

    return buf;
}
