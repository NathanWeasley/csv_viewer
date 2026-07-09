#include "code_viewer/viewer/viewer_lib.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <algorithm>
#include <cctype>
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

    return m_data.LoadFromCSV(config);
}

// ============================================================
// Clear — clear all loaded data
// ============================================================
void Viewer::Clear()
{
    m_plots.clearAll();
    m_data.Clear();
    m_lastError.clear();
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

        // After first file: capture the expected schema
        if (!expectedColNames)
        {
            expectedColCount = m_data.GetColumnCount();
            expectedColNames = &m_data.GetColumnNames();
        }
    }

    if (skipInvalidFiles && !skippedFiles.isEmpty())
        emit LoadSkippedFiles(skippedFiles);

    emit LoadFinished();
}

} // namespace viewer
