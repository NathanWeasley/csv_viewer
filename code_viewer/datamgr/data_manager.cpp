#include "code_viewer/datamgr/data_manager.h"
#include <sstream>
#include <cstring>
#include <cctype>

namespace viewer
{

// ============================================================
// CsvRowReader 实现
// ============================================================

CsvRowReader::CsvRowReader(const std::string& filePath, char delimiter, char quote)
    : m_filePath(filePath)
    , m_delimiter(delimiter)
    , m_quote(quote)
{
}

bool CsvRowReader::open()
{
    m_stream.open(m_filePath, std::ios::binary);
    if (!m_stream.is_open())
        return false;

    m_lineNum = 0;
    m_totalLines = 0;
    return true;
}

void CsvRowReader::close()
{
    if (m_stream.is_open())
        m_stream.close();
}

bool CsvRowReader::reset()
{
    close();
    return open();
}

bool CsvRowReader::readRow(std::vector<std::string>& fields)
{
    fields.clear();
    std::string line;

    while (std::getline(m_stream, line))
    {
        ++m_lineNum;

        // 跳过空行
        if (line.empty())
            continue;

        // 去除 CR（Windows 换行符）
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // 处理引号内的换行
        // 检查行中引号数量，如果是奇数，说明引号跨行
        int quoteCount = 0;
        for (char c : line)
            if (c == m_quote) ++quoteCount;

        std::string fullLine = line;
        while (quoteCount % 2 != 0 && std::getline(m_stream, line))
        {
            ++m_lineNum;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            fullLine += '\n';
            fullLine += line;
            for (char c : line)
                if (c == m_quote) ++quoteCount;
        }

        // 解析这一行
        parseLine(fullLine, fields);
        return true;
    }

    return false;
}

bool CsvRowReader::parseLine(const std::string& line, std::vector<std::string>& fields)
{
    fields.clear();

    if (line.empty())
        return true;

    std::string current;
    bool inQuotes = false;
    size_t i = 0;

    while (i < line.size())
    {
        char c = line[i];

        if (inQuotes)
        {
            if (c == m_quote)
            {
                // 处理引号内的双引号转义
                if (i + 1 < line.size() && line[i + 1] == m_quote)
                {
                    current += m_quote;
                    i += 2;
                    continue;
                }
                inQuotes = false;
            }
            else
            {
                current += c;
            }
            ++i;
        }
        else
        {
            if (c == m_quote)
            {
                inQuotes = true;
                ++i;
            }
            else if (c == m_delimiter)
            {
                fields.push_back(current);
                current.clear();
                ++i;
            }
            else
            {
                current += c;
                ++i;
            }
        }
    }

    fields.push_back(current);  // 最后一个字段
    return true;
}

// ============================================================
// DataManager 实现
// ============================================================

bool DataManager::LoadFromCSV(const LoadConfig& config)
{
    // 清理旧数据
    Clear();

    m_filePath = config.filePath;

    // ----- 阶段 0: 第一次遍历 - 统计总行数用于进度条 -----
    CsvRowReader counter(config.filePath, config.delimiter, config.quoteChar);
    if (!counter.open())
    {
        Clear();
        return false;
    }

    const auto reportProgress = [&](float p, const std::string& stage, const std::string& detail) {
        if (config.progressCb)
            config.progressCb(p, stage, detail);
    };

    reportProgress(0.0f, "Counting lines...", m_filePath);

    uint64_t totalDataRows = 0;
    {
        std::vector<std::string> tmpFields;
        while (counter.readRow(tmpFields))
        {
            ++totalDataRows;
            if (totalDataRows % 100000 == 0)
                reportProgress(0.0f, "Counting lines...", std::to_string(totalDataRows));
        }
    }
    counter.close();

    // 减去表头行
    if (config.hasHeader && totalDataRows > 0)
    {
        if (static_cast<uint64_t>(config.headerRow) < totalDataRows)
            totalDataRows -= 1;  // 减去表头
    }

    if (totalDataRows == 0)
    {
        Clear();
        return false;
    }

    // ----- 阶段 1: 读取表头 + 第一轮数据 - 确定列数并扫描类型 -----
    CsvRowReader scanner(m_filePath, config.delimiter, config.quoteChar);
    if (!scanner.open())
    {
        Clear();
        return false;
    }

    reportProgress(0.02f, "Reading headers...", "");

    // 读取表头
    std::vector<std::string> headerFields;
    if (config.hasHeader)
    {
        // 跳到表头行
        for (int i = 0; i <= config.headerRow; ++i)
        {
            if (!scanner.readRow(headerFields))
                break;
        }
    }

    size_t colCount = headerFields.size();
    if (colCount == 0)
    {
        // 无表头：用第一行数据来确定列数，但还没读到数据
        // 先读一行确定列数
        std::vector<std::string> firstRow;
        if (!scanner.readRow(firstRow))
        {
            Clear();
            return false;
        }
        colCount = firstRow.size();
        // 这时我们已经读了一行数据，需要重新扫描
        scanner.reset();
        if (config.hasHeader)
        {
            for (int i = 0; i <= config.headerRow; ++i)
                scanner.readRow(headerFields);
        }
    }

    if (colCount == 0)
    {
        Clear();
        return false;
    }

    // 生成列名
    std::vector<std::string> rawNames;
    if (config.hasHeader && !headerFields.empty())
    {
        rawNames = headerFields;
    }
    else
    {
        // 无表头：生成 Col_0, Col_1, ...
        rawNames.resize(colCount);
        for (size_t c = 0; c < colCount; ++c)
            rawNames[c] = "Col_" + std::to_string(c);
    }

    m_rawColumnNames = rawNames;
    sanitizeColumnNames(rawNames, m_columnNames, m_nameIndex);

    // ----- 阶段 1b: 扫描所有行的类型 -----
    reportProgress(0.05f, "Scanning data types...", "");

    std::vector<TypeCount> typeCounters(colCount);

    uint64_t scannedRows = 0;
    std::vector<std::string> rowFields;

    // 如果上次没有读到数据行，需要先跳到表头后的第一行
    // 如果已经读了第一行数据（无表头情况），typeCounters 已经更新了那行
    // 重新从 scanner 读
    scanner.reset();
    if (config.hasHeader)
    {
        for (int i = 0; i <= config.headerRow; ++i)
            scanner.readRow(rowFields);  // 跳过表头
    }
    rowFields.clear();

    const uint64_t PROGRESS_INTERVAL = std::max<uint64_t>(1, totalDataRows / 200);  // 每 0.5% 更新一次

    while (scanner.readRow(rowFields))
    {
        size_t n = std::min(rowFields.size(), colCount);
        for (size_t c = 0; c < n; ++c)
        {
            CellType ct = classifyCell(rowFields[c]);
            switch (ct)
            {
            case CellType::Int:    ++typeCounters[c].intCount; break;
            case CellType::Float:  ++typeCounters[c].floatCount; break;
            case CellType::String: ++typeCounters[c].stringCount; break;
            }
        }
        ++scannedRows;

        if (scannedRows % PROGRESS_INTERVAL == 0)
        {
            float p = 0.05f + 0.45f * static_cast<float>(scannedRows) / static_cast<float>(totalDataRows);
            reportProgress(p, "Scanning data types (pass 1/2)...",
                           std::to_string(scannedRows) + " / " + std::to_string(totalDataRows));
        }
    }

    // 推断每列类型
    std::vector<ColumnType> colTypes(colCount, ColumnType::Float64);
    for (size_t c = 0; c < colCount; ++c)
    {
        colTypes[c] = resolveColumnType(typeCounters[c]);
    }

    // ----- 阶段 2: 第二遍读取 - 数据写入 -----
    reportProgress(0.52f, "Loading data (pass 2/2)...", "");

    // 创建列
    m_columns.resize(colCount);
    for (size_t c = 0; c < colCount; ++c)
    {
        if (colTypes[c] == ColumnType::Int64)
            m_columns[c] = std::make_unique<Column<int64_t>>(ColumnType::Int64);
        else
            m_columns[c] = std::make_unique<Column<double>>(ColumnType::Float64);
    }

    CsvRowReader writer(m_filePath, config.delimiter, config.quoteChar);
    if (!writer.open())
    {
        Clear();
        return false;
    }

    // 跳过表头
    if (config.hasHeader)
    {
        for (int i = 0; i <= config.headerRow; ++i)
            writer.readRow(rowFields);
    }

    uint64_t writtenRows = 0;
    rowFields.clear();

    while (writer.readRow(rowFields))
    {
        size_t n = std::min(rowFields.size(), colCount);
        for (size_t c = 0; c < n; ++c)
        {
            m_columns[c]->pushFromString(rowFields[c]);
        }

        // 如果有些字段缺失，补齐 NaN 或 0
        for (size_t c = n; c < colCount; ++c)
        {
            if (colTypes[c] == ColumnType::Int64)
                m_columns[c]->pushFromString("0");
            else
                m_columns[c]->pushFromString("");
        }

        ++writtenRows;
        if (writtenRows % PROGRESS_INTERVAL == 0)
        {
            float p = 0.52f + 0.47f * static_cast<float>(writtenRows) / static_cast<float>(totalDataRows);
            reportProgress(p, "Loading data (pass 2/2)...",
                           std::to_string(writtenRows) + " / " + std::to_string(totalDataRows));
        }
    }

    // 自动检测横轴
    m_xAxisColumn = AutoDetectXAxis();

    reportProgress(1.0f, "Done.", "Loaded " + std::to_string(colCount) + " columns, " +
                  std::to_string(GetRowCount()) + " rows.");

    return true;
}

void DataManager::Clear()
{
    m_columns.clear();
    m_columnNames.clear();
    m_rawColumnNames.clear();
    m_nameIndex.clear();
    m_filePath.clear();
    m_xAxisColumn = npos;
}

// ---- 列访问 ----

AbstractColumn* DataManager::GetColumn(size_t idx)
{
    if (idx >= m_columns.size())
        return nullptr;
    return m_columns[idx].get();
}

const AbstractColumn* DataManager::GetColumn(size_t idx) const
{
    if (idx >= m_columns.size())
        return nullptr;
    return m_columns[idx].get();
}

AbstractColumn* DataManager::GetColumn(const std::string& name)
{
    size_t idx = GetColumnIndex(name);
    if (idx == npos) return nullptr;
    return GetColumn(idx);
}

const AbstractColumn* DataManager::GetColumn(const std::string& name) const
{
    size_t idx = GetColumnIndex(name);
    if (idx == npos) return nullptr;
    return GetColumn(idx);
}

size_t DataManager::GetColumnIndex(const std::string& name) const
{
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end())
        return it->second;
    return npos;
}

size_t DataManager::GetRowCount() const noexcept
{
    if (m_columns.empty()) return 0;
    return m_columns[0]->size();
}

// ---- 行访问 ----

std::vector<double> DataManager::GetRowAsDoubles(size_t rowIdx) const
{
    std::vector<double> result;
    result.reserve(m_columns.size());
    for (size_t c = 0; c < m_columns.size(); ++c)
    {
        result.push_back(m_columns[c]->getDouble(rowIdx));
    }
    return result;
}

double DataManager::GetValueAsDouble(size_t colIdx, size_t rowIdx) const
{
    if (colIdx >= m_columns.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_columns[colIdx]->getDouble(rowIdx);
}

double DataManager::GetValueAsDouble(const std::string& colName, size_t rowIdx) const
{
    size_t idx = GetColumnIndex(colName);
    if (idx == npos)
        return std::numeric_limits<double>::quiet_NaN();
    return GetValueAsDouble(idx, rowIdx);
}

// ---- 列名处理 ----

void DataManager::sanitizeColumnNames(
    const std::vector<std::string>& rawNames,
    std::vector<std::string>& outSanitized,
    std::unordered_map<std::string, size_t>& outIndex)
{
    outSanitized.clear();
    outSanitized.reserve(rawNames.size());
    outIndex.clear();

    for (size_t i = 0; i < rawNames.size(); ++i)
    {
        std::string name = rawNames[i];

        // 清洗：只保留字母、数字、下划线
        std::string cleaned;
        bool firstChar = true;
        for (char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            {
                cleaned += c;
                firstChar = false;
            }
            else if (firstChar && std::isdigit(static_cast<unsigned char>(c)))
            {
                // 首字符是数字 -> 加 "col_" 前缀
                cleaned = "col_" + name;
                break;
            }
            else if (c == ' ' || c == '\t')
            {
                // 空格跳过（保留，但如果一直空格导致 cleaned 为空则用下划线）
                if (!cleaned.empty())
                    cleaned += '_';
            }
            else
            {
                // 其他字符跳过
                continue;
            }
        }

        // 如果是空字符串或全是特殊字符
        if (cleaned.empty())
            cleaned = "col_" + std::to_string(i);

        // 去除末尾下划线
        while (!cleaned.empty() && cleaned.back() == '_')
            cleaned.pop_back();

        if (cleaned.empty())
            cleaned = "col_" + std::to_string(i);

        // 检查重名
        std::string finalName = cleaned;
        int suffix = 0;
        while (outIndex.find(finalName) != outIndex.end())
        {
            ++suffix;
            finalName = cleaned + "_" + std::to_string(suffix);
        }

        outSanitized.push_back(finalName);
        outIndex[finalName] = i;
    }
}

// ---- 类型推断 ----

ColumnType DataManager::resolveColumnType(const TypeCount& tc) const noexcept
{
    if (tc.stringCount > 0)
        return ColumnType::Float64;  // 含不可解析字符串 → Float64（存 NaN）

    if (tc.floatCount > 0)
        return ColumnType::Float64;  // 有浮点数 → Float64

    if (tc.intCount > 0)
        return ColumnType::Int64;    // 全为整数 → Int64

    return ColumnType::Float64;      // 空列默认 Float64
}

// ---- 横轴检测 ----

size_t DataManager::AutoDetectXAxis() const
{
    // 关键词（不区分大小写）
    static const std::vector<std::string> keywords = {
        "time", "timestamp", "datetime", "date", "t",
        "time_ms", "time_s", "utc", "epoch",
        "x", "index", "id"
    };

    if (m_columnNames.empty())
        return npos;

    // 对每列名计算匹配得分
    struct Match {
        size_t index;
        int score;
    };
    std::vector<Match> matches;

    for (size_t i = 0; i < m_columnNames.size(); ++i)
    {
        const std::string& name = m_columnNames[i];
        std::string lowerName;
        lowerName.reserve(name.size());
        for (char c : name)
            lowerName += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        int score = 0;
        for (const auto& kw : keywords)
        {
            if (lowerName == kw)
            {
                score += 100;  // 完全匹配
            }
            else if (lowerName.find(kw) != std::string::npos)
            {
                score += 50;   // 包含关键词
            }
        }
        matches.push_back({i, score});
    }

    // 找最高分
    size_t bestIdx = npos;
    int bestScore = 0;
    for (const auto& m : matches)
    {
        if (m.score > bestScore)
        {
            bestScore = m.score;
            bestIdx = m.index;
        }
    }

    return bestIdx;
}

// ---- 类型升级 ----

size_t DataManager::UpgradeColumnToFloat64(size_t colIdx)
{
    if (colIdx >= m_columns.size())
        return npos;

    AbstractColumn* col = m_columns[colIdx].get();
    if (col->type() != ColumnType::Int64)
        return colIdx;  // 已经 Float64，无需升级

    // 创建 Float64 列
    auto floatCol = std::make_unique<Column<double>>(ColumnType::Float64);

    // 拷贝数据
    col->copyToDoubleColumn(floatCol.get());

    // 替换
    m_columns[colIdx] = std::move(floatCol);

    return colIdx;
}

} // namespace viewer