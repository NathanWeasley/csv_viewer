#include "code_logparse/ziplog/zip_entry_binary_input.h"

#include <zip.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace viewer::logparse::ziplog
{
namespace
{

zip_t* openArchiveReadOnly(const std::filesystem::path& path, std::string& errorText)
{
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = nullptr;
#ifdef _WIN32
    source = zip_source_win32w_create(path.c_str(), 0, -1, &error);
#else
    source = zip_source_file_create(path.c_str(), 0, -1, &error);
#endif
    if (!source)
    {
        errorText = zip_error_strerror(&error);
        zip_error_fini(&error);
        return nullptr;
    }
    zip_t* archive = zip_open_from_source(source, ZIP_RDONLY, &error);
    if (!archive)
    {
        errorText = zip_error_strerror(&error);
        zip_source_free(source);
    }
    zip_error_fini(&error);
    return archive;
}

} // namespace

struct ZipEntryBinaryInput::Impl
{
    zip_t* archive = nullptr;
    zip_file_t* file = nullptr;
};

ZipEntryBinaryInput::ZipEntryBinaryInput(std::filesystem::path archivePath,
                                         ZipEntryInfo entry)
    : m_impl(std::make_unique<Impl>())
    , m_archivePath(std::move(archivePath))
    , m_entry(std::move(entry))
{
}

ZipEntryBinaryInput::~ZipEntryBinaryInput()
{
    close();
}

bool ZipEntryBinaryInput::open()
{
    close();
    m_lastError.clear();
    if (!m_entry.canRead())
    {
        m_lastError = "ZIP entry is not a supported regular, unencrypted file";
        return false;
    }
    return reopenEntry();
}

bool ZipEntryBinaryInput::reopenEntry()
{
    if (!m_impl->archive)
    {
        m_impl->archive = openArchiveReadOnly(m_archivePath, m_lastError);
        if (!m_impl->archive)
            return false;
    }
    if (m_impl->file)
    {
        zip_fclose(m_impl->file);
        m_impl->file = nullptr;
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(m_impl->archive, m_entry.index, ZIP_FL_ENC_GUESS, &stat) != 0
        || !(stat.valid & ZIP_STAT_NAME) || !(stat.valid & ZIP_STAT_SIZE)
        || !stat.name
        || m_entry.pathUtf8 != [&]()
            {
                std::string name(stat.name);
                std::replace(name.begin(), name.end(), '\\', '/');
                return name;
            }()
        || m_entry.uncompressedSize != stat.size
        || ((stat.valid & ZIP_STAT_CRC) && m_entry.crc != stat.crc))
    {
        m_lastError = "ZIP archive changed after its directory was read";
        return false;
    }

    m_impl->file = zip_fopen_index(m_impl->archive, m_entry.index, 0);
    if (!m_impl->file)
    {
        m_lastError = zip_strerror(m_impl->archive);
        return false;
    }
    m_seekable = zip_file_is_seekable(m_impl->file) == 1;
    m_offset = 0;
    return true;
}

void ZipEntryBinaryInput::close() noexcept
{
    if (!m_impl)
        return;
    if (m_impl->file)
    {
        zip_fclose(m_impl->file);
        m_impl->file = nullptr;
    }
    if (m_impl->archive)
    {
        zip_discard(m_impl->archive);
        m_impl->archive = nullptr;
    }
    m_offset = 0;
    m_seekable = false;
}

bool ZipEntryBinaryInput::isOpen() const noexcept
{
    return m_impl && m_impl->archive && m_impl->file;
}

bool ZipEntryBinaryInput::read(void* destination, size_t byteCount)
{
    if (byteCount == 0)
        return true;
    if (!isOpen() || !destination || byteCount > m_entry.uncompressedSize - m_offset)
    {
        m_lastError = "invalid or out-of-range ZIP entry read";
        return false;
    }

    auto* output = static_cast<unsigned char*>(destination);
    size_t remaining = byteCount;
    while (remaining > 0)
    {
        const zip_uint64_t chunk = static_cast<zip_uint64_t>(remaining);
        const zip_int64_t count = zip_fread(m_impl->file, output, chunk);
        if (count <= 0)
        {
            m_lastError = zip_file_strerror(m_impl->file);
            return false;
        }
        output += static_cast<size_t>(count);
        remaining -= static_cast<size_t>(count);
        m_offset += static_cast<uint64_t>(count);
    }
    return true;
}

bool ZipEntryBinaryInput::skipForward(uint64_t byteCount)
{
    std::array<unsigned char, 64 * 1024> discard{};
    while (byteCount > 0)
    {
        const size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(byteCount, discard.size()));
        if (!read(discard.data(), chunk))
            return false;
        byteCount -= chunk;
    }
    return true;
}

bool ZipEntryBinaryInput::seek(uint64_t absoluteOffset)
{
    if (!isOpen() || absoluteOffset > m_entry.uncompressedSize)
    {
        m_lastError = "invalid or out-of-range ZIP entry seek";
        return false;
    }
    if (absoluteOffset == m_offset)
        return true;

    if (m_seekable
        && absoluteOffset <= static_cast<uint64_t>(std::numeric_limits<zip_int64_t>::max()))
    {
        if (zip_fseek(m_impl->file, static_cast<zip_int64_t>(absoluteOffset), SEEK_SET) == 0)
        {
            m_offset = absoluteOffset;
            return true;
        }
        // A failed native seek does not guarantee that the stream position was
        // preserved. Reopen before using the portable discard fallback.
        if (!reopenEntry())
            return false;
    }

    if (absoluteOffset < m_offset && !reopenEntry())
        return false;
    return skipForward(absoluteOffset - m_offset);
}

std::filesystem::path ZipEntryBinaryInput::displayPath() const
{
    return std::filesystem::path(m_archivePath.wstring()
        + L"::" + std::filesystem::u8path(m_entry.pathUtf8).wstring());
}

} // namespace viewer::logparse::ziplog
