#include "code_viewer/viewer/viewer_lib.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include "code_logparse/ziplog/zip_archive.h"
#include <QFileInfo>
#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <unordered_set>

namespace viewer
{

Viewer::Viewer(QObject* parent)
    : QObject(parent)
{
}

// ============================================================
// detectHeader — heuristic to decide if row 0 is a header
// ============================================================
bool Viewer::detectHeader(const std::vector<std::string>& firstRow)
{
    if (firstRow.empty())
        return false;

    // 只有全部单元格都可解析为数字，才判定为非表头（数据行）
    for (const auto& cell : firstRow)
    {
        if (cell.empty()) return true;  // 空单元格 → 表头
        char* end = nullptr;
        std::strtod(cell.c_str(), &end);
        if (!(end != cell.c_str() && *end == '\0'))
            return true;  // 不能解析为数字 → 表头
    }

    return false;  // 全部可解析为数字 → 非表头
}

// ============================================================
// sanitizeColumnNames — clean up raw header names
// ============================================================
static std::string sanitizeSingleName(std::string raw)
{
    // Trim leading/trailing whitespace
    size_t start = 0;
    while (start < raw.size() && (raw[start] == ' ' || raw[start] == '\t' || raw[start] == '\r' || raw[start] == '\n'))
        ++start;

    size_t end = raw.size();
    while (end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t' || raw[end - 1] == '\r' || raw[end - 1] == '\n'))
        --end;

    std::string cleaned = raw.substr(start, end - start);

    // Replace special characters with '_'
    for (char& c : cleaned)
    {
        if (c == ' ' || c == ',' || c == '"' || c == '\'' || c == ';' || c == '|' ||
            c == '\\' || c == '/' || c == '(' || c == ')' || c == '[' ||
            c == ']' || c == '{' || c == '}' || c == '<' || c == '>' ||
            c == ':' || c == '*' || c == '?' || c == '#' || c == '@' ||
            c == '!' || c == '&' || c == '=' || c == '~' || c == '^' ||
            c == '%' || c == '$' || c == '\n' || c == '\r' || c == '\t')
        {
            c = '_';
        }
    }

    // Collapse consecutive underscores
    std::string result;
    bool lastWasUnderscore = false;
    for (char c : cleaned)
    {
        if (c == '_')
        {
            if (!lastWasUnderscore)
                result += c;
            lastWasUnderscore = true;
        }
        else
        {
            result += c;
            lastWasUnderscore = false;
        }
    }

    // Trim leading/trailing underscores
    while (!result.empty() && result.front() == '_')
        result.erase(0, 1);
    while (!result.empty() && result.back() == '_')
        result.pop_back();

    return result;
}

std::vector<std::string> Viewer::sanitizeColumnNames(const std::vector<std::string>& rawNames)
{
    std::vector<std::string> result;
    std::unordered_set<std::string> used;

    for (size_t i = 0; i < rawNames.size(); ++i)
    {
        std::string name = sanitizeSingleName(rawNames[i]);

        // Fall back to "col_N" if empty after sanitization
        if (name.empty())
            name = "col_" + std::to_string(i + 1);

        // Ensure uniqueness
        std::string candidate = name;
        int suffix = 2;
        while (used.count(candidate))
        {
            candidate = name + "_" + std::to_string(suffix++);
        }

        used.insert(candidate);
        result.push_back(candidate);
    }

    return result;
}

// ============================================================
// LoadCSV — main entry point
// ============================================================
bool Viewer::LoadCSV(const std::string& path)
{
    return LoadCSV(path, ',', '"');
}

bool Viewer::LoadCSV(const std::string& path, char delimiter, char quote)
{
    m_lastError.clear();

    // Step 1: Read the first row to detect header
    CsvRowReader scanner(std::filesystem::path(path), delimiter, quote);
    if (!scanner.open())
    {
        m_lastError = "Failed to open file: " + path;
        return false;
    }

    std::vector<std::string> firstRow;
    if (!scanner.readRow(firstRow) || firstRow.empty())
    {
        m_lastError = "File is empty or unreadable: " + path;
        return false;
    }
    scanner.close();

    // Step 2: Detect if first row is a header
    bool hasHeader = detectHeader(firstRow);
    std::vector<std::string> rawColumnNames;

    if (hasHeader)
    {
        rawColumnNames = firstRow;
    }
    else
    {
        rawColumnNames.resize(firstRow.size());
        for (size_t i = 0; i < firstRow.size(); ++i)
            rawColumnNames[i] = "col_" + std::to_string(i + 1);
    }

    // Step 3: Sanitize column names
    std::vector<std::string> cleanNames = sanitizeColumnNames(rawColumnNames);

    // Step 4: Build LoadConfig and delegate to DataManager
    LoadConfig config;
    config.filePath         = path;
    config.headerRow        = 0;
    config.hasHeader        = hasHeader;
    config.delimiter        = delimiter;
    config.quoteChar        = quote;
    config.progressCb       = nullptr;
    config.preSanitizedNames = cleanNames;  // Pass pre-sanitized names

    const bool loaded = m_data.LoadFromCSV(config);
    if (loaded)
    {
        activateLoadSession(plugin::SourceType::Csv, QString::fromUtf8(path.c_str()));
    }
    return loaded;
}

bool Viewer::AdoptBinaryLog(logparse::ParseResult&& result,
                            const std::string& sourcePath)
{
    m_lastError.clear();
    if (!result.success() || result.columns.empty() || result.timestampCount == 0)
    {
        m_lastError = "Binary log parsing did not produce a valid dataset.";
        return false;
    }

    std::vector<std::string> names;
    std::vector<std::vector<double>> values;
    names.reserve(result.columns.size());
    values.reserve(result.columns.size());
    for (auto& parsedColumn : result.columns)
    {
        names.push_back(std::move(parsedColumn.name));
        values.push_back(std::move(parsedColumn.values));
    }

    // All large column buffers have moved out. Release schema/diagnostic/range
    // storage before handing ownership to DataManager.
    result = logparse::ParseResult{};

    if (GetDataManager().GetColumnCount() > 0)
        Clear();
    if (!m_data.LoadFromColumns(std::move(names), std::move(values), sourcePath))
    {
        m_lastError = "Failed to import parsed binary-log columns.";
        return false;
    }

    const QString qSourcePath = QString::fromUtf8(sourcePath.c_str());
    const plugin::SourceType sourceType = qSourcePath.endsWith(
        QStringLiteral(".zip"), Qt::CaseInsensitive)
        ? plugin::SourceType::Zip
        : plugin::SourceType::BinaryLog;
    activateLoadSession(sourceType, qSourcePath);

    emit LoadFinished();
    return true;
}

bool Viewer::AddDerivedColumn(quint64 sessionId,
                              const QString& name,
                              std::vector<double>&& values,
                              QString* error)
{
    if (!m_loadSession.isValid() || sessionId != m_loadSession.sessionId)
    {
        if (error) *error = QStringLiteral("The loaded dataset changed before the column was committed.");
        return false;
    }

    const std::string utf8Name = name.toUtf8().toStdString();
    const AddDerivedColumnStatus status = m_data.AddDerivedColumn(
        utf8Name, std::move(values));
    switch (status)
    {
    case AddDerivedColumnStatus::Success:
        emit DataColumnAdded(sessionId, name);
        return true;
    case AddDerivedColumnStatus::InvalidName:
        if (error) *error = QStringLiteral("The derived column name is empty.");
        break;
    case AddDerivedColumnStatus::DuplicateName:
        if (error) *error = QStringLiteral("A data item with the same name already exists.");
        break;
    case AddDerivedColumnStatus::RowCountMismatch:
        if (error) *error = QStringLiteral("The derived column row count does not match the loaded dataset.");
        break;
    }
    return false;
}

void Viewer::activateLoadSession(plugin::SourceType sourceType,
                                 const QString& sourcePath)
{
    if (m_nextLoadSessionId == 0)
        m_nextLoadSessionId = 1;
    m_loadSession.sessionId = m_nextLoadSessionId++;
    m_loadSession.sourceType = sourceType;
    m_loadSession.sourcePath = sourcePath;
    m_loadSession.sourceFileName = QFileInfo(sourcePath).fileName();
    emit DataLoaded(m_loadSession.sessionId);
}

bool Viewer::ReadZipCatalog(
    const std::filesystem::path& archivePath,
    std::vector<logparse::ziplog::ZipEntryInfo>& entries,
    std::string& error)
{
    logparse::ziplog::ZipArchive archive;
    if (!archive.open(archivePath))
    {
        error = archive.lastError();
        entries.clear();
        return false;
    }
    entries = archive.entries();
    error.clear();
    return true;
}

logparse::ParseResult Viewer::ParseZipEntries(
    const std::filesystem::path& archivePath,
    const std::vector<uint64_t>& entryIndices,
    const logparse::ParseOptions& options)
{
    logparse::ziplog::ZipArchive archive;
    if (!archive.open(archivePath))
    {
        logparse::ParseResult result;
        result.diagnostics.push_back({
            logparse::DiagnosticSeverity::Error, archivePath, 0,
            "failed to open ZIP archive: " + archive.lastError()});
        return result;
    }

    std::vector<std::unique_ptr<logparse::BinaryInput>> inputs;
    inputs.reserve(entryIndices.size());
    for (const uint64_t index : entryIndices)
    {
        auto input = archive.createInput(index);
        if (!input)
        {
            logparse::ParseResult result;
            result.diagnostics.push_back({
                logparse::DiagnosticSeverity::Error, archivePath, 0,
                "selected ZIP entry cannot be opened (index "
                    + std::to_string(index) + ")"});
            return result;
        }
        inputs.push_back(std::move(input));
    }

    logparse::BinaryLogParser parser;
    return parser.parseInputs(std::move(inputs), options);
}

bool Viewer::ReadZipEntry(const std::filesystem::path& archivePath,
                          uint64_t entryIndex,
                          QByteArray& bytes,
                          std::string& error)
{
    bytes.clear();
    error.clear();

    logparse::ziplog::ZipArchive archive;
    if (!archive.open(archivePath))
    {
        error = archive.lastError();
        return false;
    }

    const auto found = std::find_if(archive.entries().begin(), archive.entries().end(),
        [entryIndex](const auto& entry) { return entry.index == entryIndex; });
    if (found == archive.entries().end() || !found->canRead())
    {
        error = "ZIP entry is missing or is not a readable regular file.";
        return false;
    }
    if (found->uncompressedSize
        > static_cast<uint64_t>(std::numeric_limits<qsizetype>::max()))
    {
        error = "ZIP entry is too large for an in-memory byte array.";
        return false;
    }

    auto input = archive.createInput(entryIndex);
    if (!input || !input->open())
    {
        error = input ? input->lastError() : "Unable to create ZIP entry reader.";
        return false;
    }
    try
    {
        bytes.resize(static_cast<qsizetype>(found->uncompressedSize));
    }
    catch (const std::bad_alloc&)
    {
        error = "Not enough memory to read ZIP entry.";
        return false;
    }
    if (!bytes.isEmpty()
        && !input->read(bytes.data(), static_cast<size_t>(bytes.size())))
    {
        error = input->lastError();
        bytes.clear();
        return false;
    }
    input->close();
    return true;
}

// ============================================================
// Clear — clear all loaded data
// ============================================================
void Viewer::Clear()
{
    if (m_loadSession.isValid())
        emit DataAboutToUnload(m_loadSession.sessionId);
    m_plots.clearAll();
    m_data.Clear();
    m_lastError.clear();
    m_loadSession = plugin::LoadSessionInfo{};
}

// ============================================================
// OnLoadCSV — slot: load multiple CSV files with two-level progress
// ============================================================
void Viewer::OnLoadCSV(const QStringList& files)
{
    OnLoadCSV(files, false);
}

void Viewer::OnLoadCSV(const QStringList& files, bool skipInvalidFiles)
{
    if (files.isEmpty())
    {
        emit LoadFinished();
        return;
    }

    // Clear existing data before loading new files
    if (GetDataManager().GetColumnCount() > 0)
        Clear();

    const int total = files.size();
    emit LoadStarted(total);

    // Stored after the first file is loaded, for cross-file validation
    size_t expectedColCount = 0;
    const std::vector<std::string>* expectedColNames = nullptr;
    QStringList skippedFiles;
    QString firstLoadedFile;

    for (int i = 0; i < total; ++i)
    {
        const std::filesystem::path path(files[i].toStdWString());
        const QString filename = files[i];
        const float fileBase = static_cast<float>(i) / static_cast<float>(total);
        const float fileWeight = 1.0f / static_cast<float>(total);

        std::vector<std::string> sanitizedNames;
        bool hasHeader = true;

        // Detect header and sanitize column names
        {
            CsvRowReader scanner(path, ',', '"');
            if (!scanner.open())
            {
                m_lastError = "Failed to open file: " + path.string();
                if (skipInvalidFiles)
                {
                    skippedFiles << filename;
                    emit BusyProgressChanged(static_cast<float>(i + 1) / static_cast<float>(total));
                    continue;
                }

                QString msg = QString("Failed to open \"%1\".").arg(filename);
                Clear();
                emit LoadError(msg);
                emit LoadFinished();
                return;
            }

            std::vector<std::string> firstRow;
            if (!scanner.readRow(firstRow) || firstRow.empty())
            {
                m_lastError = "File is empty or unreadable: " + path.string();
                scanner.close();
                if (skipInvalidFiles)
                {
                    skippedFiles << filename;
                    emit BusyProgressChanged(static_cast<float>(i + 1) / static_cast<float>(total));
                    continue;
                }

                QString msg = QString("File is empty or unreadable: \"%1\".").arg(filename);
                Clear();
                emit LoadError(msg);
                emit LoadFinished();
                return;
            }
            scanner.close();

            hasHeader = detectHeader(firstRow);
            std::vector<std::string> rawColumnNames;

            if (hasHeader)
                rawColumnNames = firstRow;
            else
            {
                rawColumnNames.resize(firstRow.size());
                for (size_t j = 0; j < firstRow.size(); ++j)
                    rawColumnNames[j] = "col_" + std::to_string(j + 1);
            }

            sanitizedNames = sanitizeColumnNames(rawColumnNames);
        }

        // ============================================================
        // Cross-file column validation (skip for the first file)
        // ============================================================
        if (i > 0 && expectedColNames)
        {
            const size_t thisColCount = sanitizedNames.size();

            if (thisColCount != expectedColCount)
            {
                if (skipInvalidFiles)
                {
                    skippedFiles << filename;
                    emit BusyProgressChanged(static_cast<float>(i + 1) / static_cast<float>(total));
                    continue;
                }
                else
                {
                    QString msg = QString("Column count mismatch in \"%1\":\n"
                                          "Expected %2 columns but got %3.")
                        .arg(filename)
                        .arg(expectedColCount)
                        .arg(thisColCount);
                    Clear();
                    emit LoadError(msg);
                    emit LoadFinished();
                    return;
                }
            }
        }

        // Build LoadConfig with a ProgressCallback that reports global progress
        LoadConfig config;
        config.filePath  = path;
        config.headerRow = 0;
        config.hasHeader = hasHeader;
        config.delimiter = ',';
        config.quoteChar = '"';
        config.preSanitizedNames = sanitizedNames;
        config.progressCb = [this, fileBase, fileWeight](float p, const std::string& /*stage*/, const std::string& /*detail*/)
        {
            float global = fileBase + p * fileWeight;
            emit BusyProgressChanged(global);
        };

        bool loaded = m_data.LoadFromCSV(config);
        if (!loaded)
        {
            if (skipInvalidFiles)
            {
                skippedFiles << filename;
                emit BusyProgressChanged(static_cast<float>(i + 1) / static_cast<float>(total));
                continue;
            }
            else
            {
                QString msg = QString("Failed to load \"%1\".").arg(filename);
                Clear();
                emit LoadError(msg);
                emit LoadFinished();
                return;
            }
        }

        if (firstLoadedFile.isEmpty())
            firstLoadedFile = filename;

        // After first file: capture the expected schema
        if (!expectedColNames)
        {
            expectedColCount = m_data.GetColumnCount();
            expectedColNames = &m_data.GetColumnNames();
        }
    }

    if (skipInvalidFiles && !skippedFiles.isEmpty())
        emit LoadSkippedFiles(skippedFiles);

    if (m_data.GetColumnCount() > 0 && !firstLoadedFile.isEmpty())
        activateLoadSession(plugin::SourceType::Csv, firstLoadedFile);

    emit LoadFinished();
}

} // namespace viewer
