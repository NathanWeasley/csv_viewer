#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace viewer::logparse
{

class BinaryReader
{
public:
    explicit BinaryReader(std::filesystem::path filePath);

    bool open();
    bool isOpen() const noexcept;

    const std::filesystem::path& filePath() const noexcept { return m_filePath; }
    uint64_t offset() const noexcept { return m_offset; }
    uint64_t size() const noexcept { return m_size; }
    uint64_t remaining() const noexcept { return m_size - m_offset; }

    bool readUInt8(uint8_t& value);
    bool readUInt16(uint16_t& value);
    bool readUInt32(uint32_t& value);
    bool readUInt64(uint64_t& value);
    bool readBytes(void* destination, size_t byteCount);
    bool readBytes(std::vector<uint8_t>& destination, size_t byteCount);
    bool skip(uint64_t byteCount);

    // Search for A5 5A from the current offset and stop just after the magic.
    bool seekNextFrameMagic(uint64_t& magicOffset);

private:
    std::filesystem::path m_filePath;
    std::ifstream m_stream;
    uint64_t m_offset = 0;
    uint64_t m_size = 0;
};

} // namespace viewer::logparse
