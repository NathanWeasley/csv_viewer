#include "code_logparse/rbt_log_parser.h"

#include "code_logparse/ziplog/zip_archive.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>

namespace viewer::logparse
{
namespace
{

constexpr std::array<uint8_t, 4> kMagic{{'$', 'A', 'G', 'V'}};
constexpr size_t kHeaderSize = 12;
constexpr size_t kIoBufferSize = 256 * 1024;

uint32_t readLittleEndianU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16)
        | (static_cast<uint32_t>(bytes[3]) << 24);
}

std::string zlibErrorText(int code, const z_stream& stream)
{
    if (stream.msg && *stream.msg)
        return stream.msg;
    switch (code)
    {
    case Z_MEM_ERROR: return "zlib ran out of memory";
    case Z_DATA_ERROR: return "invalid RFC 1950 compressed stream";
    case Z_STREAM_ERROR: return "invalid zlib stream state";
    case Z_VERSION_ERROR: return "incompatible zlib runtime";
    case Z_BUF_ERROR: return "truncated RFC 1950 compressed stream";
    default: return "zlib inflate failed with code " + std::to_string(code);
    }
}

std::string sanitizeOutputName(const std::string& sourcePath)
{
    std::string name = std::filesystem::u8path(sourcePath).filename().u8string();
    if (name.empty())
        name = "RBT";
    for (char& character : name)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        if (value < 0x20 || character == '<' || character == '>'
            || character == ':' || character == '"' || character == '/'
            || character == '\\' || character == '|' || character == '?'
            || character == '*')
        {
            character = '_';
        }
    }
    return name + ".log";
}

std::string caseFoldAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    });
    return value;
}

class NormalizedTextWriter final
{
public:
    NormalizedTextWriter(std::ofstream& output, uint64_t maximumBytes)
        : m_output(output), m_maximumBytes(maximumBytes)
    {
        m_buffer.reserve(kIoBufferSize + 1);
    }

    bool append(const uint8_t* bytes, size_t size)
    {
        m_buffer.clear();
        for (size_t index = 0; index < size; ++index)
        {
            const char value = static_cast<char>(bytes[index]);
            if (m_pendingCarriageReturn)
            {
                if (value == '\n')
                {
                    m_buffer.push_back('\n');
                    m_buffer.push_back('\n');
                    m_pendingCarriageReturn = false;
                    continue;
                }
                m_buffer.push_back('\r');
                m_pendingCarriageReturn = false;
            }
            if (value == '\r')
                m_pendingCarriageReturn = true;
            else
                m_buffer.push_back(value);
        }
        return writeBuffer();
    }

    bool finish()
    {
        m_buffer.clear();
        if (m_pendingCarriageReturn)
        {
            m_buffer.push_back('\r');
            m_pendingCarriageReturn = false;
        }
        return writeBuffer();
    }

    uint64_t bytesWritten() const noexcept { return m_bytesWritten; }
    const std::string& error() const noexcept { return m_error; }

private:
    bool writeBuffer()
    {
        if (m_buffer.empty())
            return true;
        if (m_bytesWritten > m_maximumBytes
            || static_cast<uint64_t>(m_buffer.size()) > m_maximumBytes - m_bytesWritten)
        {
            m_error = "decompressed RBT log exceeds the configured output limit";
            return false;
        }
        m_output.write(m_buffer.data(), static_cast<std::streamsize>(m_buffer.size()));
        if (!m_output)
        {
            m_error = "failed to write decompressed RBT text";
            return false;
        }
        m_bytesWritten += static_cast<uint64_t>(m_buffer.size());
        return true;
    }

    std::ofstream& m_output;
    uint64_t m_maximumBytes = 0;
    uint64_t m_bytesWritten = 0;
    bool m_pendingCarriageReturn = false;
    std::vector<char> m_buffer;
    std::string m_error;
};

bool cancelled(const RbtLogParseOptions& options)
{
    return options.isCancelled && options.isCancelled();
}

} // namespace

size_t RbtLogBatchResult::successCount() const noexcept
{
    return static_cast<size_t>(std::count_if(files.begin(), files.end(),
        [](const RbtLogFileResult& file) { return file.success(); }));
}

RbtLogFileResult RbtLogParser::parse(
    BinaryInput& input,
    const std::filesystem::path& outputPath,
    const RbtLogParseOptions& options) const
{
    RbtLogFileResult result;
    result.sourcePath = input.displayPath();
    result.outputPath = outputPath;
    result.inputBytes = input.size();

    const auto failEarly = [&](std::string error)
    {
        result.error = std::move(error);
        input.close();
        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        return result;
    };

    if (!input.open())
        return failEarly("unable to open RBT input: " + input.lastError());
    if (input.size() < kHeaderSize)
        return failEarly("RBT input is shorter than one block header");

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        return failEarly("unable to create the decompressed RBT text file");
    const auto fail = [&](std::string error)
    {
        result.error = std::move(error);
        input.close();
        output.close();
        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        return result;
    };
    NormalizedTextWriter textWriter(output, options.maximumOutputBytes);

    std::array<uint8_t, kHeaderSize> header{};
    std::array<uint8_t, kIoBufferSize> compressed{};
    std::array<uint8_t, kIoBufferSize> decompressed{};

    while (input.tell() < input.size())
    {
        if (cancelled(options))
            return fail("RBT parsing cancelled");

        const uint64_t blockOffset = input.tell();
        const uint64_t bytesRemaining = input.size() - blockOffset;
        if (bytesRemaining < kHeaderSize)
            return fail("truncated RBT block header at byte " + std::to_string(blockOffset));
        if (!input.read(header.data(), header.size()))
            return fail("unable to read RBT block header at byte " + std::to_string(blockOffset));
        if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()))
            return fail("invalid RBT block magic at byte " + std::to_string(blockOffset));

        const uint32_t compressedSize = readLittleEndianU32(header.data() + 8);
        if (compressedSize == 0)
            return fail("RBT block has an empty compressed payload at byte "
                        + std::to_string(blockOffset));
        if (static_cast<uint64_t>(compressedSize) > input.size() - input.tell())
            return fail("truncated RBT compressed payload at byte "
                        + std::to_string(blockOffset));

        z_stream stream{};
        int status = inflateInit(&stream);
        if (status != Z_OK)
            return fail(zlibErrorText(status, stream));

        uint64_t payloadRemaining = compressedSize;
        bool streamEnded = false;
        std::string inflateError;
        while (payloadRemaining > 0 && inflateError.empty())
        {
            if (cancelled(options))
            {
                inflateError = "RBT parsing cancelled";
                break;
            }
            const size_t chunkSize = static_cast<size_t>(std::min<uint64_t>(
                payloadRemaining, compressed.size()));
            if (!input.read(compressed.data(), chunkSize))
            {
                inflateError = "unable to read RBT compressed payload";
                break;
            }
            payloadRemaining -= chunkSize;
            if (options.progress)
                options.progress(result.sourcePath, input.tell(), input.size(), 0, 1);
            stream.next_in = compressed.data();
            stream.avail_in = static_cast<uInt>(chunkSize);

            while (stream.avail_in > 0)
            {
                stream.next_out = decompressed.data();
                stream.avail_out = static_cast<uInt>(decompressed.size());
                status = inflate(&stream, Z_NO_FLUSH);
                const size_t produced = decompressed.size() - stream.avail_out;
                if (produced > 0 && !textWriter.append(decompressed.data(), produced))
                {
                    inflateError = textWriter.error();
                    break;
                }
                if (status == Z_STREAM_END)
                {
                    streamEnded = true;
                    if (stream.avail_in != 0 || payloadRemaining != 0)
                        inflateError = "RBT block contains bytes after the RFC 1950 stream";
                    break;
                }
                if (status != Z_OK)
                {
                    inflateError = zlibErrorText(status, stream);
                    break;
                }
                if (produced == 0 && stream.avail_out != 0)
                {
                    inflateError = "RFC 1950 stream made no decompression progress";
                    break;
                }
            }
        }
        if (inflateError.empty() && !streamEnded)
            inflateError = "truncated RFC 1950 compressed stream";
        inflateEnd(&stream);
        if (!inflateError.empty())
            return fail(inflateError + " in block " + std::to_string(result.blockCount));

        ++result.blockCount;
    }

    if (!textWriter.finish())
        return fail(textWriter.error());
    output.close();
    if (!output)
        return fail("failed to finalize decompressed RBT text");
    input.close();
    result.outputBytes = textWriter.bytesWritten();
    return result;
}

RbtLogBatchResult RbtLogParser::parseZipEntries(
    const std::filesystem::path& archivePath,
    const std::vector<uint64_t>& entryIndices,
    const std::filesystem::path& outputDirectory,
    const RbtLogParseOptions& options) const
{
    RbtLogBatchResult batch;
    ziplog::ZipArchive archive;
    if (!archive.open(archivePath))
    {
        batch.archiveError = archive.lastError();
        return batch;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory, directoryError);
    if (directoryError)
    {
        batch.archiveError = "unable to create RBT output directory: "
            + directoryError.message();
        return batch;
    }

    std::set<std::string> usedNames;
    batch.files.reserve(entryIndices.size());
    for (size_t inputIndex = 0; inputIndex < entryIndices.size(); ++inputIndex)
    {
        if (cancelled(options))
        {
            batch.cancelled = true;
            break;
        }

        const uint64_t entryIndex = entryIndices[inputIndex];
        const auto entry = std::find_if(archive.entries().begin(), archive.entries().end(),
            [entryIndex](const ziplog::ZipEntryInfo& candidate)
            {
                return candidate.index == entryIndex;
            });
        if (entry == archive.entries().end() || !entry->canRead())
        {
            RbtLogFileResult failed;
            failed.entryIndex = entryIndex;
            failed.error = "selected RBT ZIP entry is missing or unreadable";
            batch.files.push_back(std::move(failed));
            continue;
        }

        std::string outputName = sanitizeOutputName(entry->pathUtf8);
        const std::string stem = outputName.substr(0, outputName.size() - 4);
        int duplicate = 2;
        while (!usedNames.insert(caseFoldAscii(outputName)).second)
            outputName = stem + "_" + std::to_string(duplicate++) + ".log";

        auto input = archive.createInput(entryIndex);
        if (!input)
        {
            RbtLogFileResult failed;
            failed.entryIndex = entryIndex;
            failed.sourcePath = std::filesystem::u8path(entry->pathUtf8);
            failed.error = "unable to create the RBT ZIP entry reader";
            batch.files.push_back(std::move(failed));
            continue;
        }

        RbtLogParseOptions fileOptions = options;
        fileOptions.progress = [&, inputIndex](const std::filesystem::path& source,
                                               uint64_t processed,
                                               uint64_t total,
                                               size_t,
                                               size_t)
        {
            if (options.progress)
            {
                options.progress(source, processed, total,
                                 inputIndex, entryIndices.size());
            }
        };
        RbtLogFileResult file = parse(
            *input, outputDirectory / std::filesystem::u8path(outputName), fileOptions);
        file.entryIndex = entryIndex;
        if (cancelled(options) && !file.success())
            batch.cancelled = true;
        batch.files.push_back(std::move(file));
        if (batch.cancelled)
            break;
    }
    return batch;
}

} // namespace viewer::logparse
