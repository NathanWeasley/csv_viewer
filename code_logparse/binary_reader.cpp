#include "code_logparse/binary_reader.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace viewer::logparse
{

BinaryReader::BinaryReader(std::filesystem::path filePath)
    : m_filePath(std::move(filePath))
{
}

bool BinaryReader::open()
{
    m_stream.close();
    m_stream.clear();
    m_stream.open(m_filePath, std::ios::binary);
    if (!m_stream.is_open())
        return false;

    m_stream.seekg(0, std::ios::end);
    const std::streamoff end = m_stream.tellg();
    if (end < 0)
    {
        m_stream.close();
        return false;
    }

    m_size = static_cast<uint64_t>(end);
    m_offset = 0;
    m_stream.seekg(0, std::ios::beg);
    return static_cast<bool>(m_stream);
}

bool BinaryReader::isOpen() const noexcept
{
    return m_stream.is_open();
}

bool BinaryReader::readUInt8(uint8_t& value)
{
    return readBytes(&value, sizeof(value));
}

bool BinaryReader::readUInt16(uint16_t& value)
{
    uint8_t bytes[2] = {};
    if (!readBytes(bytes, sizeof(bytes)))
        return false;
    value = static_cast<uint16_t>(bytes[0])
          | (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

bool BinaryReader::readUInt32(uint32_t& value)
{
    uint8_t bytes[4] = {};
    if (!readBytes(bytes, sizeof(bytes)))
        return false;
    value = static_cast<uint32_t>(bytes[0])
          | (static_cast<uint32_t>(bytes[1]) << 8)
          | (static_cast<uint32_t>(bytes[2]) << 16)
          | (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool BinaryReader::readUInt64(uint64_t& value)
{
    uint8_t bytes[8] = {};
    if (!readBytes(bytes, sizeof(bytes)))
        return false;

    value = 0;
    for (size_t i = 0; i < sizeof(bytes); ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    return true;
}

bool BinaryReader::readBytes(void* destination, size_t byteCount)
{
    if (byteCount == 0)
        return true;
    if (!destination || byteCount > remaining()
        || byteCount > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return false;
    }

    m_stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(byteCount));
    if (!m_stream)
        return false;

    m_offset += static_cast<uint64_t>(byteCount);
    return true;
}

bool BinaryReader::readBytes(std::vector<uint8_t>& destination, size_t byteCount)
{
    try
    {
        destination.resize(byteCount);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
    return readBytes(destination.data(), byteCount);
}

bool BinaryReader::skip(uint64_t byteCount)
{
    if (byteCount > remaining()
        || byteCount > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
    {
        return false;
    }

    m_stream.seekg(static_cast<std::streamoff>(byteCount), std::ios::cur);
    if (!m_stream)
        return false;

    m_offset += byteCount;
    return true;
}

bool BinaryReader::seekNextFrameMagic(uint64_t& magicOffset)
{
    uint8_t previous = 0;
    bool havePrevious = false;

    while (remaining() > 0)
    {
        uint8_t current = 0;
        if (!readUInt8(current))
            return false;

        if (havePrevious && previous == 0xA5 && current == 0x5A)
        {
            magicOffset = m_offset - 2;
            return true;
        }

        previous = current;
        havePrevious = true;
    }

    return false;
}

} // namespace viewer::logparse
