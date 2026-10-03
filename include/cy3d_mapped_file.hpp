// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <filesystem>
#include <stdexcept>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace cy {
// Read-only mapping: input bytes stay in the OS page cache, with no extra file-sized copy.
class MappedFile {
    const uint8_t* data_ = nullptr;
    uint64_t size_ = 0;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE, mapping_ = nullptr;
#else
    int file_ = -1;
#endif
    void close() {
#ifdef _WIN32
        if (data_)
            UnmapViewOfFile(data_);
        if (mapping_)
            CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
#else
        if (data_)
            munmap(const_cast<uint8_t*>(data_), static_cast<size_t>(size_));
        if (file_ >= 0)
            ::close(file_);
#endif
    }

  public:
    explicit MappedFile(const std::filesystem::path& path) {
#ifdef _WIN32
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (file_ != INVALID_HANDLE_VALUE && GetFileSizeEx(file_, &size) && size.QuadPart > 0) {
            size_ = static_cast<uint64_t>(size.QuadPart);
            mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping_)
                data_ = static_cast<const uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        }
#else
        file_ = ::open(path.c_str(), O_RDONLY);
        struct stat info {};
        if (file_ >= 0 && !fstat(file_, &info) && info.st_size > 0) {
            size_ = static_cast<uint64_t>(info.st_size);
            auto mapped = mmap(nullptr, static_cast<size_t>(size_), PROT_READ, MAP_PRIVATE, file_, 0);
            if (mapped != MAP_FAILED)
                data_ = static_cast<const uint8_t*>(mapped);
        }
#endif
        if (!data_) {
            close();
            throw std::runtime_error("Fichier vide ou inaccessible");
        }
    }
    ~MappedFile() { close(); }
    MappedFile(const MappedFile&) = delete;
    const uint8_t* data() const { return data_; }
    uint64_t size() const { return size_; }
};
} // namespace cy
