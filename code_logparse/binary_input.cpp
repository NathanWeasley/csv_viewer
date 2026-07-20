#include "code_logparse/binary_input.h"

#include <limits>

namespace viewer::logparse
{

LocalFileBinaryInput::LocalFileBinaryInput(std::filesystem::path filePath)
    : m_filePath(std::move(filePath))
{
}

LocalFileBinaryInput::~LocalFileBinaryInput()
{
    close();
}

bool LocalFileBinaryInput::open()
{
    close();
    m_lastError.clear();
    m_stream.open(m_filePath, std::ios::binary);
    if (!m_stream.is_open())
    {
        m_lastError = "failed to open file";
        return false;
    }

    m_stream.seekg(0, std::ios::end);
    const std::streamoff end = m_stream.tellg();
    if (end < 0)
    {
        m_lastError = "failed to determine file size";
        close();
        return false;
    }

    m_size = static_cast<uint64_t>(end);
    m_offset = 0;
    m_stream.seekg(0, std::ios::beg);
    if (!m_stream)
    {
        m_lastError = "failed to seek to the beginning of file";
        close();
        return false;
    }
    return true;
}

void LocalFileBinaryInput::close() noexcept
{
    m_stream.close();
    m_stream.clear();
    m_offset = 0;
    m_size = 0;
}

bool LocalFileBinaryInput::isOpen() const noexcept
{
    return m_stream.is_open();
}

bool LocalFileBinaryInput::read(void* destination, size_t byteCount)
{
    if (byteCount == 0)
        return true;
    if (!isOpen() || !destination || byteCount > m_size - m_offset
        || byteCount > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        m_lastError = "invalid or out-of-range read";
        return false;
    }

    m_stream.read(static_cast<char*>(destination), static_cast<std::streamsize>(byteCount));
    if (!m_stream)
    {
        m_lastError = "failed while reading file";
        return false;
    }
    m_offset += static_cast<uint64_t>(byteCount);
    return true;
}

bool LocalFileBinaryInput::seek(uint64_t absoluteOffset)
{
    if (!isOpen() || absoluteOffset > m_size
        || absoluteOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
    {
        m_lastError = "invalid or out-of-range seek";
        return false;
    }

    m_stream.clear();
    m_stream.seekg(static_cast<std::streamoff>(absoluteOffset), std::ios::beg);
    if (!m_stream)
    {
        m_lastError = "failed to seek in file";
        return false;
    }
    m_offset = absoluteOffset;
    return true;
}

} // namespace viewer::logparse
