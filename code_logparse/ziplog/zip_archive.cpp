#include "code_logparse/ziplog/zip_archive.h"

#include "code_logparse/ziplog/zip_entry_binary_input.h"

#include <zip.h>

#include <algorithm>
#include <cctype>
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

std::string normalizeEntryPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool isUnsafeEntryPath(const std::string& path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\')
        return true;
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':')
    {
        return true;
    }

    size_t begin = 0;
    while (begin <= path.size())
    {
        const size_t end = path.find_first_of("/\\", begin);
        const size_t count = (end == std::string::npos) ? path.size() - begin : end - begin;
        if (path.compare(begin, count, "..") == 0)
            return true;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return false;
}

bool isUnixSymlink(zip_t* archive, uint64_t index)
{
    zip_uint8_t operatingSystem = 0;
    zip_uint32_t attributes = 0;
    if (zip_file_get_external_attributes(archive, index, 0,
                                         &operatingSystem, &attributes) != 0)
    {
        return false;
    }
    if (operatingSystem != ZIP_OPSYS_UNIX && operatingSystem != ZIP_OPSYS_OS_X)
        return false;
    constexpr uint32_t kUnixFileTypeMask = 0170000u;
    constexpr uint32_t kUnixSymlink = 0120000u;
    return ((attributes >> 16) & kUnixFileTypeMask) == kUnixSymlink;
}

} // namespace

struct ZipArchive::Impl
{
    zip_t* archive = nullptr;
};

ZipArchive::ZipArchive()
    : m_impl(std::make_unique<Impl>())
{
}

ZipArchive::~ZipArchive()
{
    close();
}

ZipArchive::ZipArchive(ZipArchive&&) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&&) noexcept = default;

bool ZipArchive::open(const std::filesystem::path& archivePath,
                      const ZipReadLimits& limits)
{
    close();
    m_lastError.clear();
    m_path = archivePath;
    m_impl->archive = openArchiveReadOnly(archivePath, m_lastError);
    if (!m_impl->archive)
        return false;

    const zip_int64_t count = zip_get_num_entries(m_impl->archive, 0);
    if (count < 0)
    {
        m_lastError = zip_strerror(m_impl->archive);
        close();
        return false;
    }
    if (static_cast<uint64_t>(count) > limits.maximumEntryCount)
    {
        m_lastError = "ZIP contains too many entries";
        close();
        return false;
    }

    uint64_t totalSize = 0;
    m_entries.reserve(static_cast<size_t>(count));
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index)
    {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(m_impl->archive, index, ZIP_FL_ENC_GUESS, &stat) != 0
            || !(stat.valid & ZIP_STAT_NAME))
        {
            m_lastError = "failed to read ZIP central-directory entry "
                + std::to_string(index) + ": " + zip_strerror(m_impl->archive);
            close();
            return false;
        }

        ZipEntryInfo entry;
        entry.index = index;
        entry.pathUtf8 = normalizeEntryPath(stat.name ? stat.name : "");
        entry.uncompressedSize = (stat.valid & ZIP_STAT_SIZE) ? stat.size : 0;
        entry.compressedSize = (stat.valid & ZIP_STAT_COMP_SIZE) ? stat.comp_size : 0;
        entry.crc = (stat.valid & ZIP_STAT_CRC) ? stat.crc : 0;
        entry.modificationTime = (stat.valid & ZIP_STAT_MTIME)
            ? static_cast<int64_t>(stat.mtime) : 0;
        entry.compressionMethod = (stat.valid & ZIP_STAT_COMP_METHOD)
            ? stat.comp_method : ZIP_CM_DEFAULT;
        entry.encryptionMethod = (stat.valid & ZIP_STAT_ENCRYPTION_METHOD)
            ? stat.encryption_method : ZIP_EM_NONE;
        entry.isDirectory = !entry.pathUtf8.empty() && entry.pathUtf8.back() == '/';
        entry.isSymlink = isUnixSymlink(m_impl->archive, index);
        entry.hasUnsafePath = isUnsafeEntryPath(entry.pathUtf8);
        entry.compressionSupported =
            zip_compression_method_supported(entry.compressionMethod, 0) != 0;

        if (!entry.isDirectory)
        {
            if (entry.uncompressedSize > limits.maximumEntrySize
                || entry.uncompressedSize > limits.maximumTotalSize - totalSize)
            {
                m_lastError = "ZIP uncompressed size exceeds the configured safety limit";
                close();
                return false;
            }
            totalSize += entry.uncompressedSize;
        }
        m_entries.push_back(std::move(entry));
    }
    return true;
}

void ZipArchive::close() noexcept
{
    if (m_impl && m_impl->archive)
    {
        zip_discard(m_impl->archive);
        m_impl->archive = nullptr;
    }
    m_entries.clear();
}

bool ZipArchive::isOpen() const noexcept
{
    return m_impl && m_impl->archive;
}

std::unique_ptr<BinaryInput> ZipArchive::createInput(uint64_t entryIndex) const
{
    const auto found = std::find_if(m_entries.begin(), m_entries.end(),
        [entryIndex](const ZipEntryInfo& entry) { return entry.index == entryIndex; });
    if (found == m_entries.end() || !found->canRead())
        return {};
    return std::make_unique<ZipEntryBinaryInput>(m_path, *found);
}

} // namespace viewer::logparse::ziplog
