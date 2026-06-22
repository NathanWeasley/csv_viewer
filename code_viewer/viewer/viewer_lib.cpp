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

    // If ALL cells classify as String, treat as header
    for (const auto& cell : firstRow)
    {
        CellType ct = classifyCell(cell);
        if (ct != CellType::String)
            return false;
    }

    return true;
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
        if (c == ',' || c == '"' || c == '\'' || c == ';' || c == '|' ||
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
    m_data.Clear();
    m_lastError.clear();
}

// ============================================================
// OnLoadCSV — slot: load multiple CSV files with two-level progress
// ============================================================
void Viewer::OnLoadCSV(const QStringList& files)
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

    for (int i = 0; i < total; ++i)
    {
        const std::filesystem::path path(files[i].toStdWString());
        const float fileBase = static_cast<float>(i) / static_cast<float>(total);
        const float fileWeight = 1.0f / static_cast<float>(total);

        // Build LoadConfig with a ProgressCallback that reports global progress
        LoadConfig config;
        config.filePath  = path;
        config.headerRow = 0;
        config.hasHeader = true;  // Will be overridden by detectHeader logic below
        config.delimiter = ',';
        config.quoteChar = '"';
        config.progressCb = [this, fileBase, fileWeight](float p, const std::string& /*stage*/, const std::string& /*detail*/)
        {
            float global = fileBase + p * fileWeight;
            emit BusyProgressChanged(global);
        };

        // Replicate the header-detection logic inline to properly set hasHeader
        // and preSanitizedNames before passing config to DataManager
        {
            CsvRowReader scanner(path, ',', '"');
            if (!scanner.open())
            {
                m_lastError = "Failed to open file: " + path.string();
                continue;
            }

            std::vector<std::string> firstRow;
            if (!scanner.readRow(firstRow) || firstRow.empty())
            {
                m_lastError = "File is empty or unreadable: " + path.string();
                scanner.close();
                continue;
            }
            scanner.close();

            bool hasHeader = detectHeader(firstRow);
            std::vector<std::string> rawColumnNames;

            if (hasHeader)
            {
                rawColumnNames = firstRow;
            }
            else
            {
                rawColumnNames.resize(firstRow.size());
                for (size_t j = 0; j < firstRow.size(); ++j)
                    rawColumnNames[j] = "col_" + std::to_string(j + 1);
            }

            config.hasHeader         = hasHeader;
            config.preSanitizedNames = sanitizeColumnNames(rawColumnNames);
        }

        m_data.LoadFromCSV(config);
    }

    emit LoadFinished();
}

} // namespace viewer
