#include "code_logparse/binary_reader.h"

#include <limits>
#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>

namespace viewer::logparse
{

BinaryReader::BinaryReader(std::filesystem::path filePath)
    : BinaryReader(std::make_unique<LocalFileBinaryInput>(std::move(filePath)))
{
}

BinaryReader::BinaryReader(std::unique_ptr<BinaryInput> input)
    : m_input(std::move(input))
{
    if (m_input)
        m_displayPath = m_input->displayPath();
}

bool BinaryReader::open()
{
    m_offset = 0;
    m_size = 0;
    m_bufferPosition = 0;
    m_bufferSize = 0;
    if (!m_input || !m_input->open())
        return false;
    m_size = m_input->size();
    return true;
}

bool BinaryReader::isOpen() const noexcept
{
    return m_input && m_input->isOpen();
}

const std::string& BinaryReader::lastError() const noexcept
{
    static const std::string empty;
    return m_input ? m_input->lastError() : empty;
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
    if (!m_input || !m_input->isOpen() || !destination || byteCount > remaining())
    {
        return false;
    }

    auto* output = static_cast<uint8_t*>(destination);
    size_t bytesLeft = byteCount;
    while (bytesLeft > 0)
    {
        const size_t buffered = m_bufferSize - m_bufferPosition;
        if (buffered > 0)
        {
            const size_t copied = std::min(buffered, bytesLeft);
            std::memcpy(output, m_buffer.data() + m_bufferPosition, copied);
            output += copied;
            bytesLeft -= copied;
            m_bufferPosition += copied;
            m_offset += copied;
            continue;
        }

        m_bufferPosition = 0;
        m_bufferSize = 0;
        if (bytesLeft >= m_buffer.size())
        {
            if (!m_input || !m_input->read(output, bytesLeft))
                return false;
            m_offset += bytesLeft;
            return true;
        }

        const uint64_t physicalRemaining = m_size - m_input->tell();
        const size_t refillSize = static_cast<size_t>(
            std::min<uint64_t>(m_buffer.size(), physicalRemaining));
        if (refillSize == 0 || !m_input->read(m_buffer.data(), refillSize))
            return false;
        m_bufferSize = refillSize;
    }
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
    if (byteCount > remaining())
    {
        return false;
    }

    const size_t buffered = m_bufferSize - m_bufferPosition;
    if (byteCount <= buffered)
    {
        m_bufferPosition += static_cast<size_t>(byteCount);
        m_offset += byteCount;
        return true;
    }

    const uint64_t target = m_offset + byteCount;
    if (!m_input || !m_input->seek(target))
        return false;
    m_bufferPosition = 0;
    m_bufferSize = 0;
    m_offset = target;
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
