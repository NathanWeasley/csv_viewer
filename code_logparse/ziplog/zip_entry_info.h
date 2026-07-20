#pragma once

#include <cstdint>
#include <string>

namespace viewer::logparse::ziplog
{

struct ZipEntryInfo
{
    uint64_t index = 0;
    std::string pathUtf8;
    uint64_t uncompressedSize = 0;
    uint64_t compressedSize = 0;
    uint32_t crc = 0;
    int64_t modificationTime = 0;
    int32_t compressionMethod = 0;
    uint16_t encryptionMethod = 0;
    bool isDirectory = false;
    bool isSymlink = false;
    bool hasUnsafePath = false;
    bool compressionSupported = true;

    bool isEncrypted() const noexcept { return encryptionMethod != 0; }
    bool canRead() const noexcept
    {
        return !isDirectory && !isSymlink && !hasUnsafePath
            && !isEncrypted() && compressionSupported;
    }
};

} // namespace viewer::logparse::ziplog
