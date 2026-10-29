/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include <bzlib.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint64_t kCompressedLimit = 128ull * 1024 * 1024;
constexpr uint64_t kExpandedLimit = 256ull * 1024 * 1024;
constexpr size_t kEntryLimit = 4096;
constexpr size_t kPathLimit = 4096;

[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }

uint64_t octal(const char *field, size_t length)
{
    size_t i = 0;
    while (i < length && (field[i] == ' ' || field[i] == '\0')) ++i;
    uint64_t value = 0;
    bool digit = false;
    for (; i < length && field[i] != '\0' && field[i] != ' '; ++i) {
        if (field[i] < '0' || field[i] > '7') fail("Invalid tar numeric field");
        digit = true;
        if (value > (UINT64_MAX - (field[i] - '0')) / 8) fail("Tar size overflow");
        value = value * 8 + (field[i] - '0');
    }
    return digit ? value : 0;
}

std::string field(const char *data, size_t length)
{
    size_t end = 0;
    while (end < length && data[end] != '\0') ++end;
    return std::string(data, end);
}

int open_directory(int parent, const std::string& name)
{
    if (mkdirat(parent, name.c_str(), 0700) < 0 && errno != EEXIST)
        fail("Cannot create extracted directory");
    int fd = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) fail("Unsafe or inaccessible extracted directory");
    return fd;
}

void extract_file(int root, const std::string& path, uint64_t size,
                  const std::function<void(char *, size_t)>& read_data)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string part = path.substr(start, end - start);
        if (part.empty() || part == ".") { start = end + 1; continue; }
        if (part == "..") fail("Unsafe archive path");
        parts.push_back(std::move(part));
        if (end == path.size()) break;
        start = end + 1;
    }
    if (parts.empty()) fail("Empty archive path");
    int dir = dup(root);
    if (dir < 0) fail("Cannot access extraction directory");
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        int next = open_directory(dir, parts[i]); close(dir); dir = next;
    }
    int out = openat(dir, parts.back().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(dir);
    if (out < 0) fail("Duplicate or unsafe archive member");
    char buffer[65536];
    uint64_t remaining = size;
    try {
        while (remaining) {
            size_t amount = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof buffer));
            read_data(buffer, amount);
            size_t offset = 0;
            while (offset < amount) {
                ssize_t n = write(out, buffer + offset, amount - offset);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) fail("Cannot write extracted file");
                offset += static_cast<size_t>(n);
            }
            remaining -= amount;
        }
    } catch (...) { close(out); throw; }
    if (close(out) < 0) fail("Cannot close extracted file");
}

template<typename Read, typename Finish>
void parse_tar(Read read, Finish finish, int root)
{
    std::array<char, 512> header{};
    uint64_t expanded = 0;
    size_t entries = 0;
    std::set<std::string> seen;
    unsigned zero_blocks = 0;
    for (;;) {
        read(header.data(), header.size());
        if (std::all_of(header.begin(), header.end(), [](char c) { return c == 0; })) {
            if (++zero_blocks == 2) { finish(); return; }
            continue;
        }
        if (zero_blocks) fail("Invalid tar end marker");
        if (++entries > kEntryLimit) fail("Archive has too many entries");
        uint64_t stored_sum = octal(header.data() + 148, 8), actual_sum = 0;
        for (size_t i = 0; i < header.size(); ++i)
            actual_sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
        if (stored_sum != actual_sum) fail("Invalid tar header checksum");
        uint64_t size = octal(header.data() + 124, 12);
        if (size > kExpandedLimit - expanded) fail("Archive exceeds expanded size limit");
        expanded += size;
        char type = header[156];
        std::string name = field(header.data(), 100);
        std::string prefix = field(header.data() + 345, 155);
        if (!prefix.empty()) name = prefix + "/" + name;
        if (name.size() > kPathLimit || name.empty() || name.front() == '/') fail("Unsafe archive path");
        if (type == '5') {
            if (size != 0) fail("Invalid directory member");
            while (!name.empty() && name.back() == '/') name.pop_back();
            if (name.empty()) fail("Invalid directory path");
            int dir = dup(root);
            size_t start = 0;
            while (start < name.size()) {
                size_t end = name.find('/', start);
                if (end == std::string::npos) end = name.size();
                auto part = name.substr(start, end - start);
                if (part == "..") { close(dir); fail("Unsafe archive path"); }
                if (!part.empty() && part != ".") { int next = open_directory(dir, part); close(dir); dir = next; }
                start = end + 1;
            }
            close(dir);
        } else if (type == '\0' || type == '0') {
            if (!seen.insert(name).second) fail("Duplicate archive member");
            extract_file(root, name, size, read);
        } else if (type == 'x' || type == 'g' || type == 'L' || type == 'K') {
            fail("PAX/GNU extended tar metadata is unsupported; use a ustar archive with supported path lengths");
        } else {
            fail("Archive contains unsupported link or special member");
        }
        uint64_t padding = (512 - size % 512) % 512;
        if (padding) { std::array<char, 512> skip{}; read(skip.data(), static_cast<size_t>(padding)); }
    }
}

void extract_gzip(int fd, int root)
{
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK) fail("Cannot initialize gzip decoder");
    std::array<unsigned char, 65536> input{}, output{};
    size_t pending_start = 0, pending_end = 0;
    uint64_t compressed_read = 0;
    bool ended = false;
    auto advance = [&] {
        if (ended) return;
        if (stream.avail_in == 0) {
            ssize_t n;
            do { n = ::read(fd, input.data(), input.size()); } while (n < 0 && errno == EINTR);
            if (n <= 0) fail("Truncated compressed archive");
            if (static_cast<uint64_t>(n) > kCompressedLimit - compressed_read)
                fail("Archive exceeds compressed size limit");
            compressed_read += static_cast<uint64_t>(n);
            stream.next_in = input.data(); stream.avail_in = static_cast<uInt>(n);
        }
        stream.next_out = output.data(); stream.avail_out = output.size();
        int status = inflate(&stream, Z_NO_FLUSH);
        pending_start = 0; pending_end = output.size() - stream.avail_out;
        if (status == Z_STREAM_END) ended = true;
        else if (status != Z_OK) fail("Invalid gzip archive");
    };
    auto read_exact = [&](char *dest, size_t length) {
        size_t copied = 0;
        while (copied < length) {
            if (pending_start == pending_end) {
                if (ended) fail("Truncated tar archive");
                advance();
                if (pending_start == pending_end && ended) fail("Truncated tar archive");
            }
            size_t take = std::min(pending_end - pending_start, length - copied);
            std::memcpy(dest + copied, output.data() + pending_start, take);
            pending_start += take; copied += take;
        }
    };
    auto finish = [&] {
        auto check_pending = [&] {
            for (size_t i = pending_start; i < pending_end; ++i)
                if (output[i] != 0) fail("Data follows tar end marker");
            pending_start = pending_end;
        };
        check_pending();
        while (!ended) { advance(); check_pending(); }
        for (uInt i = 0; i < stream.avail_in; ++i)
            if (stream.next_in[i] != 0) fail("Trailing data follows gzip archive");
        char tail[4096]; ssize_t n;
        while ((n = ::read(fd, tail, sizeof tail)) > 0)
            for (ssize_t i = 0; i < n; ++i) if (tail[i] != 0) fail("Trailing data follows gzip archive");
        if (n < 0 && errno != EINTR) fail("Cannot finish reading gzip archive");
    };
    try { parse_tar(read_exact, finish, root); inflateEnd(&stream); }
    catch (...) { inflateEnd(&stream); throw; }
}

void extract_bzip2(int fd, int root)
{
    bz_stream stream{};
    if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK) fail("Cannot initialize bzip2 decoder");
    std::array<char, 65536> input{}, output{};
    size_t pending_start = 0, pending_end = 0;
    uint64_t compressed_read = 0;
    bool ended = false;
    auto advance = [&] {
        if (ended) return;
        if (stream.avail_in == 0) {
            ssize_t n;
            do { n = ::read(fd, input.data(), input.size()); } while (n < 0 && errno == EINTR);
            if (n <= 0) fail("Truncated compressed archive");
            if (static_cast<uint64_t>(n) > kCompressedLimit - compressed_read)
                fail("Archive exceeds compressed size limit");
            compressed_read += static_cast<uint64_t>(n);
            stream.next_in = input.data(); stream.avail_in = static_cast<unsigned int>(n);
        }
        stream.next_out = output.data(); stream.avail_out = static_cast<unsigned int>(output.size());
        int status = BZ2_bzDecompress(&stream);
        pending_start = 0; pending_end = output.size() - stream.avail_out;
        if (status == BZ_STREAM_END) ended = true;
        else if (status != BZ_OK) fail("Invalid bzip2 archive");
    };
    auto read_exact = [&](char *dest, size_t length) {
        size_t copied = 0;
        while (copied < length) {
            if (pending_start == pending_end) {
                if (ended) fail("Truncated tar archive");
                advance();
                if (pending_start == pending_end && ended) fail("Truncated tar archive");
            }
            size_t take = std::min(pending_end - pending_start, length - copied);
            std::memcpy(dest + copied, output.data() + pending_start, take);
            pending_start += take; copied += take;
        }
    };
    auto finish = [&] {
        auto check_pending = [&] {
            for (size_t i = pending_start; i < pending_end; ++i)
                if (output[i] != 0) fail("Data follows tar end marker");
            pending_start = pending_end;
        };
        check_pending();
        while (!ended) { advance(); check_pending(); }
        for (unsigned int i = 0; i < stream.avail_in; ++i)
            if (stream.next_in[i] != 0) fail("Trailing data follows bzip2 archive");
        char tail[4096]; ssize_t n;
        while ((n = ::read(fd, tail, sizeof tail)) > 0)
            for (ssize_t i = 0; i < n; ++i) if (tail[i] != 0) fail("Trailing data follows bzip2 archive");
        if (n < 0 && errno != EINTR) fail("Cannot finish reading bzip2 archive");
    };
    try { parse_tar(read_exact, finish, root); BZ2_bzDecompressEnd(&stream); }
    catch (...) { BZ2_bzDecompressEnd(&stream); throw; }
}
}

int main(int argc, char **argv)
{
    if (argc != 3) { std::fprintf(stderr, "Usage: %s ARCHIVE DESTINATION\n", argv[0]); return 2; }
    try {
        int fd = open(argv[1], O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) fail("Cannot open archive without following links");
        struct stat st{};
        if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<uint64_t>(st.st_size) > kCompressedLimit) {
            close(fd); fail("Archive is not a regular file or exceeds compressed size limit");
        }
        int root = open(argv[2], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (root < 0) { close(fd); fail("Cannot open private extraction directory"); }
        std::string archive = argv[1];
        if (archive.size() >= 7 && archive.compare(archive.size()-7, 7, ".tar.gz") == 0) extract_gzip(fd, root);
        else if (archive.size() >= 8 && archive.compare(archive.size()-8, 8, ".tar.bz2") == 0) extract_bzip2(fd, root);
        else fail("Unsupported archive format");
        close(root); close(fd); return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
