#include "code_viewer/datamgr/data_manager.h"
#include "code_viewer/datamgr/custom_expr/diff_func.h"
#include "code_viewer/base/exprtk_keywords.h"
#include "extra/exprtk/exprtk.hpp"
#include <sstream>
#include <cstring>
#include <cctype>

namespace viewer
{

// ============================================================
// CsvRowReader 实现
// ============================================================

CsvRowReader::CsvRowReader(const std::filesystem::path& filePath, char delimiter, char quote)
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
                // Trim leading/trailing whitespace from the field
                while (!current.empty() && (current.back() == ' ' || current.back() == '\t'))
                    current.pop_back();
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

    // Trim leading/trailing whitespace from the last field
    while (!current.empty() && (current.back() == ' ' || current.back() == '\t'))
        current.pop_back();
    fields.push_back(current);
    return true;
}

// ============================================================
// DataManager 实现
// ============================================================

bool DataManager::LoadFromColumns(std::vector<std::string> columnNames,
                                  std::vector<std::vector<double>> values,
                                  const std::string& sourcePath)
{
    return LoadFromColumns(std::move(columnNames), std::move(values), {}, {}, sourcePath);
}

bool DataManager::LoadFromColumns(
    std::vector<std::string> columnNames,
    std::vector<std::vector<double>> values,
    std::vector<std::string> stringColumnNames,
    std::vector<std::vector<std::string>> stringValues,
    const std::string& sourcePath)
{
    if (columnNames.size() != values.size()
        || stringColumnNames.size() != stringValues.size()
        || (columnNames.empty() && stringColumnNames.empty()))
        return false;

    const size_t rowCount = !values.empty()
        ? values.front().size() : stringValues.front().size();
    if (rowCount == 0)
        return false;
    for (const auto& column : values)
    {
        if (column.size() != rowCount)
            return false;
    }
    for (const auto& column : stringValues)
    {
        if (column.size() != rowCount)
            return false;
    }

    std::vector<std::string> displayNames = columnNames;
    for (auto& name : displayNames)
    {
        const auto alias = m_aliasMap.find(name);
        if (alias != m_aliasMap.end())
            name = alias->second;
    }

    std::unordered_map<std::string, size_t> nameIndex;
    for (size_t index = 0; index < displayNames.size(); ++index)
    {
        if (displayNames[index].empty() || nameIndex.count(displayNames[index]) != 0)
            return false;
        nameIndex.emplace(displayNames[index], index);
    }

    std::unordered_map<std::string, size_t> stringNameIndex;
    for (size_t index = 0; index < stringColumnNames.size(); ++index)
    {
        const auto& name = stringColumnNames[index];
        if (name.empty() || stringNameIndex.count(name) != 0
            || nameIndex.count(name) != 0)
        {
            return false;
        }
        stringNameIndex.emplace(name, index);
    }

    std::vector<std::shared_ptr<Column>> columns;
    columns.reserve(values.size());
    for (auto& valuesForColumn : values)
    {
        auto column = std::make_shared<Column>(std::move(valuesForColumn));
        column->recalcMinMax();
        columns.push_back(std::move(column));
    }

    std::vector<std::shared_ptr<StringColumn>> stringColumns;
    stringColumns.reserve(stringValues.size());
    for (auto& valuesForColumn : stringValues)
        stringColumns.push_back(std::make_shared<StringColumn>(std::move(valuesForColumn)));

    Clear();
    m_columns = std::move(columns);
    m_rawColumnNames = std::move(columnNames);
    m_columnNames = std::move(displayNames);
    m_columnMetadata.assign(m_columns.size(), ColumnMetadata{});
    m_nameIndex = std::move(nameIndex);
    m_stringColumns = std::move(stringColumns);
    m_stringColumnNames = std::move(stringColumnNames);
    m_rawStringColumnNames = m_stringColumnNames;
    m_stringNameIndex = std::move(stringNameIndex);
    m_filePath = sourcePath;
    m_xAxisColumn = AutoDetectXAxis();
    return true;
}

AddDerivedColumnStatus DataManager::AddDerivedColumn(std::string name,
                                                      std::vector<double>&& values)
{
    if (name.empty())
        return AddDerivedColumnStatus::InvalidName;
    if (m_nameIndex.find(name) != m_nameIndex.end()
        || m_stringNameIndex.find(name) != m_stringNameIndex.end())
        return AddDerivedColumnStatus::DuplicateName;
    if (m_columns.empty() || values.size() != GetRowCount())
        return AddDerivedColumnStatus::RowCountMismatch;

    auto column = std::make_shared<Column>(std::move(values));
    column->recalcMinMax();

    const size_t index = m_columns.size();
    m_columns.push_back(std::move(column));
    m_columnNames.push_back(name);
    m_rawColumnNames.push_back(name);
    m_columnMetadata.push_back({ColumnOrigin::PluginDerived, {}});
    m_nameIndex.emplace(std::move(name), index);
    return AddDerivedColumnStatus::Success;
}

PublishDerivedColumnsStatus DataManager::PublishPluginDerivedColumns(
    const std::string& ownerPluginId,
    std::vector<std::string> names,
    std::vector<std::vector<double>> values,
    std::vector<std::shared_ptr<Column>>* removedColumns)
{
    if (ownerPluginId.empty())
        return PublishDerivedColumnsStatus::InvalidProvider;
    if (names.size() != values.size())
        return PublishDerivedColumnsStatus::RowCountMismatch;

    const size_t rowCount = GetRowCount();
    const std::string previousXAxisName = m_xAxisColumn < m_columnNames.size()
        ? m_columnNames[m_xAxisColumn] : std::string{};
    std::unordered_set<std::string> incomingNames;
    incomingNames.reserve(names.size());
    for (size_t index = 0; index < names.size(); ++index)
    {
        const std::string& name = names[index];
        if (name.empty() || !incomingNames.insert(name).second)
            return PublishDerivedColumnsStatus::InvalidName;
        if (values[index].size() != rowCount)
            return PublishDerivedColumnsStatus::RowCountMismatch;

        if (m_stringNameIndex.find(name) != m_stringNameIndex.end())
            return PublishDerivedColumnsStatus::NameCollision;

        const auto existing = m_nameIndex.find(name);
        if (existing != m_nameIndex.end())
        {
            const size_t existingIndex = existing->second;
            const ColumnMetadata* metadata = existingIndex < m_columnMetadata.size()
                ? &m_columnMetadata[existingIndex] : nullptr;
            if (!metadata || metadata->origin != ColumnOrigin::PluginDerived
                || metadata->ownerPluginId != ownerPluginId)
            {
                return PublishDerivedColumnsStatus::NameCollision;
            }
        }
    }

    std::vector<std::shared_ptr<Column>> incomingColumns;
    incomingColumns.reserve(values.size());
    for (auto& columnValues : values)
    {
        auto column = std::make_shared<Column>(std::move(columnValues));
        column->recalcMinMax();
        incomingColumns.push_back(std::move(column));
    }

    std::vector<std::shared_ptr<Column>> nextColumns;
    std::vector<std::string> nextNames;
    std::vector<std::string> nextRawNames;
    std::vector<ColumnMetadata> nextMetadata;
    nextColumns.reserve(m_columns.size() + incomingColumns.size());
    nextNames.reserve(m_columnNames.size() + names.size());
    nextRawNames.reserve(m_rawColumnNames.size() + names.size());
    nextMetadata.reserve(m_columns.size() + names.size());
    if (removedColumns)
        removedColumns->clear();

    for (size_t index = 0; index < m_columns.size(); ++index)
    {
        const ColumnMetadata metadata = index < m_columnMetadata.size()
            ? m_columnMetadata[index] : ColumnMetadata{};
        if (metadata.origin == ColumnOrigin::PluginDerived
            && metadata.ownerPluginId == ownerPluginId)
        {
            if (removedColumns)
                removedColumns->push_back(m_columns[index]);
            continue;
        }
        nextColumns.push_back(m_columns[index]);
        nextNames.push_back(m_columnNames[index]);
        nextRawNames.push_back(index < m_rawColumnNames.size()
            ? m_rawColumnNames[index] : m_columnNames[index]);
        nextMetadata.push_back(metadata);
    }

    for (size_t index = 0; index < names.size(); ++index)
    {
        nextColumns.push_back(std::move(incomingColumns[index]));
        nextNames.push_back(names[index]);
        nextRawNames.push_back(names[index]);
        nextMetadata.push_back({ColumnOrigin::PluginDerived, ownerPluginId});
    }

    std::unordered_map<std::string, size_t> nextNameIndex;
    nextNameIndex.reserve(nextNames.size());
    for (size_t index = 0; index < nextNames.size(); ++index)
        nextNameIndex.emplace(nextNames[index], index);

    m_columns = std::move(nextColumns);
    m_columnNames = std::move(nextNames);
    m_rawColumnNames = std::move(nextRawNames);
    m_columnMetadata = std::move(nextMetadata);
    m_nameIndex = std::move(nextNameIndex);
    const auto previousXAxis = m_nameIndex.find(previousXAxisName);
    if (previousXAxis != m_nameIndex.end())
        m_xAxisColumn = previousXAxis->second;
    else
        m_xAxisColumn = AutoDetectXAxis();
    return PublishDerivedColumnsStatus::Success;
}

bool DataManager::LoadFromCSV(const LoadConfig& config)
{
    const bool isFirstLoad = m_sourceColumnNames.empty() && GetRowCount() == 0;

    const auto parseValue = [](const std::string& text) noexcept
    {
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        return (end != text.c_str() && *end == '\0')
            ? value
            : std::numeric_limits<double>::quiet_NaN();
    };

    const auto reportProgress = [&](float p, const std::string& stage, const std::string& detail)
    {
        if (config.progressCb)
            config.progressCb(p, stage, detail);
    };

    // ================================================================
    // 辅助: 读取表头字段
    // ================================================================
    auto readHeader = [&](CsvRowReader& reader, std::vector<std::string>& outFields) -> size_t
    {
        outFields.clear();
        if (config.hasHeader)
        {
            for (int i = 0; i <= config.headerRow; ++i)
            {
                if (!reader.readRow(outFields))
                    break;
            }
            return outFields.size();
        }
        // 无表头: 读第一行数据来确定列数
        std::vector<std::string> firstRow;
        if (!reader.readRow(firstRow))
            return 0;
        size_t colCount = firstRow.size();
        reader.reset();
        if (config.hasHeader)
        {
            for (int i = 0; i <= config.headerRow; ++i)
                reader.readRow(outFields);
        }
        return colCount;
    };

    // ================================================================
    // 第一阶段: 统计总行数
    // ================================================================
    CsvRowReader counter(config.filePath, config.delimiter, config.quoteChar);
    if (!counter.open())
        return false;

    reportProgress(0.0f, "Counting lines...", config.filePath.string());

    uint64_t totalDataRows = 0;
    size_t scannedColumnCount = 0;
    std::vector<bool> detectedStringColumns;
    {
        std::vector<std::string> tmpFields;
        uint64_t recordIndex = 0;
        while (counter.readRow(tmpFields))
        {
            const bool headerRecord = config.hasHeader
                && recordIndex <= static_cast<uint64_t>(std::max(0, config.headerRow));
            if (headerRecord)
            {
                if (recordIndex == static_cast<uint64_t>(std::max(0, config.headerRow)))
                    scannedColumnCount = tmpFields.size();
                ++recordIndex;
                continue;
            }

            if (scannedColumnCount == 0)
                scannedColumnCount = tmpFields.size();
            if (tmpFields.size() != scannedColumnCount)
                return false;
            if (detectedStringColumns.empty())
                detectedStringColumns.assign(scannedColumnCount, false);
            for (size_t column = 0; column < tmpFields.size(); ++column)
            {
                const std::string& text = tmpFields[column];
                if (text.empty())
                    continue;
                char* end = nullptr;
                std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0')
                    detectedStringColumns[column] = true;
            }
            ++totalDataRows;
            ++recordIndex;
        }
    }
    counter.close();

    if (totalDataRows == 0)
        return false;

    // ================================================================
    // 第二阶段: 读取表头
    // ================================================================
    CsvRowReader scanner(config.filePath, config.delimiter, config.quoteChar);
    if (!scanner.open())
        return false;

    reportProgress(0.02f, "Reading headers...", "");

    std::vector<std::string> headerFields;
    size_t colCount = readHeader(scanner, headerFields);
    if (colCount == 0)
        return false;
    if (scannedColumnCount != 0 && scannedColumnCount != colCount)
        return false;

    // ================================================================
    // 列名校验（后续 CSV 加载时）
    // ================================================================
    std::vector<std::string> newSanitizedNames;

    // preSanitizedNames 由 Viewer::LoadCSV/OnLoadCSV 始终传入
    newSanitizedNames = config.preSanitizedNames;
    if (newSanitizedNames.size() != colCount)
        return false;
    if (detectedStringColumns.size() != colCount)
        detectedStringColumns.assign(colCount, false);
    for (size_t column = 0; column < colCount; ++column)
    {
        if (!m_dateAxisName.empty() && newSanitizedNames[column] == m_dateAxisName)
            detectedStringColumns[column] = true;
    }
    std::vector<std::string> displayNames = newSanitizedNames;
    if (!m_aliasMap.empty())
    {
        for (size_t c = 0; c < displayNames.size(); ++c)
        {
            auto rawIt = (c < headerFields.size()) ? m_aliasMap.find(headerFields[c]) : m_aliasMap.end();
            if (rawIt != m_aliasMap.end())
            {
                displayNames[c] = rawIt->second;
                continue;
            }

            auto cleanIt = m_aliasMap.find(newSanitizedNames[c]);
            if (cleanIt != m_aliasMap.end())
                displayNames[c] = cleanIt->second;
        }
    }

    std::unordered_set<std::string> numericDisplayNames;
    for (size_t i = 0; i < displayNames.size(); ++i)
    {
        const bool installedString = !isFirstLoad
            && i < m_sourceColumnIsString.size() && m_sourceColumnIsString[i];
        if (detectedStringColumns[i] || installedString)
            continue;
        if (displayNames[i].empty() || !numericDisplayNames.insert(displayNames[i]).second)
            return false;
    }

    if (!isFirstLoad)
    {
        // ---- 后续 CSV: 校验列数 & 列名 ----
        if (colCount != m_sourceColumnNames.size())
            return false;

        for (size_t i = 0; i < m_sourceColumnNames.size(); ++i)
        {
            if (i >= newSanitizedNames.size())
                return false;
            if (newSanitizedNames[i] != m_sourceColumnNames[i])
                return false;
            // Empty-only files may look numeric, so only reject a newly found
            // string value in a column whose installed schema is numeric.
            if (detectedStringColumns[i] && !m_sourceColumnIsString[i])
                return false;
        }

        // ---- 后续 CSV: 直接追加数据 ----
        reportProgress(0.05f, "Appending data...", config.filePath.string());

        scanner.reset();
        // 跳过表头
        if (config.hasHeader)
        {
            for (int i = 0; i <= config.headerRow; ++i)
                scanner.readRow(headerFields);
        }

        const uint64_t PROGRESS_INTERVAL = std::max<uint64_t>(1, totalDataRows / 200);
        uint64_t appendedRows = 0;
        std::vector<std::string> rowFields;

        // 验证行内字段数一致
        uint64_t validationCount = 0;
        std::vector<std::string> validateFields;
        CsvRowReader validator(config.filePath, config.delimiter, config.quoteChar);
        if (!validator.open())
            return false;
        if (config.hasHeader)
        {
            for (int i = 0; i <= config.headerRow; ++i)
                validator.readRow(validateFields);
        }
        // 先验证各行字段数
        while (validator.readRow(validateFields))
        {
            if (validateFields.size() != colCount)
                return false;  // CSV 文件内部列数不一致
            ++validationCount;
        }
        validator.close();

        const size_t oldRowCount = GetRowCount();
        const size_t newRowCount = oldRowCount + static_cast<size_t>(validationCount);
        for (auto& column : m_columns)
            column->beginOverwrite(newRowCount);
        for (auto& column : m_stringColumns)
            column->resize(newRowCount);

        while (scanner.readRow(rowFields))
        {
            size_t n = rowFields.size();
            if (n != colCount)
                return false;

            for (size_t c = 0; c < colCount; ++c)
            {
                const size_t row = oldRowCount + static_cast<size_t>(appendedRows);
                if (m_sourceColumnIsString[c])
                    (*m_stringColumns[m_sourceToString[c]])[row] = rowFields[c];
                else
                    (*m_columns[m_sourceToNumeric[c]])[row] = parseValue(rowFields[c]);
            }

            ++appendedRows;
            if (appendedRows % PROGRESS_INTERVAL == 0)
            {
                float p = 0.05f + 0.90f * static_cast<float>(appendedRows) / static_cast<float>(totalDataRows);
                reportProgress(p, "Appending data...",
                               std::to_string(appendedRows) + " / " + std::to_string(totalDataRows));
            }
        }

        // 追加数据后重新计算每列的 min/max 缓存
        for (size_t c = 0; c < m_columns.size(); ++c)
            m_columns[c]->recalcMinMax();

        reportProgress(1.0f, "Done.", "Appended " + std::to_string(totalDataRows) + " rows.");

        return true;
    }

    // ================================================================
    // 首个 CSV 加载: 单遍扫描直接写入（不区分字符串/数值列）
    // ================================================================

    m_filePath = config.filePath.string();
    m_sourceColumnNames = newSanitizedNames;
    m_sourceColumnIsString = detectedStringColumns;
    m_sourceToNumeric.assign(colCount, npos);
    m_sourceToString.assign(colCount, npos);

    std::vector<std::string> rawNames = headerFields;
    if (!config.hasHeader || rawNames.empty())
    {
        rawNames.resize(colCount);
        for (size_t c = 0; c < colCount; ++c)
            rawNames[c] = "col_" + std::to_string(c + 1);
    }

    m_nameIndex.clear();
    m_stringNameIndex.clear();
    for (size_t sourceColumn = 0; sourceColumn < colCount; ++sourceColumn)
    {
        if (m_sourceColumnIsString[sourceColumn])
        {
            const size_t index = m_stringColumns.size();
            const std::string& name = newSanitizedNames[sourceColumn];
            if (name.empty() || m_stringNameIndex.count(name) != 0
                || m_nameIndex.count(name) != 0)
            {
                return false;
            }
            m_sourceToString[sourceColumn] = index;
            m_stringColumnNames.push_back(name);
            m_rawStringColumnNames.push_back(rawNames[sourceColumn]);
            m_stringNameIndex.emplace(name, index);
            m_stringColumns.push_back(std::make_shared<StringColumn>(
                static_cast<size_t>(totalDataRows)));
        }
        else
        {
            const size_t index = m_columns.size();
            const std::string& name = displayNames[sourceColumn];
            if (name.empty() || m_nameIndex.count(name) != 0
                || m_stringNameIndex.count(name) != 0)
            {
                return false;
            }
            m_sourceToNumeric[sourceColumn] = index;
            m_columnNames.push_back(name);
            m_rawColumnNames.push_back(rawNames[sourceColumn]);
            m_nameIndex.emplace(name, index);
            auto column = std::make_shared<Column>(static_cast<size_t>(totalDataRows));
            column->beginOverwrite();
            m_columns.push_back(std::move(column));
            m_columnMetadata.push_back(ColumnMetadata{});
        }
    }

    scanner.reset();
    if (config.hasHeader)
    {
        std::vector<std::string> skipFields;
        for (int i = 0; i <= config.headerRow; ++i)
            scanner.readRow(skipFields);
    }

    const uint64_t PROGRESS_INTERVAL = std::max<uint64_t>(1, totalDataRows / 200);
    uint64_t loadedRows = 0;
    std::vector<std::string> rowFields;

    reportProgress(0.05f, "Loading data...", "");

    while (scanner.readRow(rowFields))
    {
        size_t n = std::min(rowFields.size(), colCount);
        for (size_t c = 0; c < n; ++c)
        {
            if (m_sourceColumnIsString[c])
                (*m_stringColumns[m_sourceToString[c]])[static_cast<size_t>(loadedRows)] =
                    rowFields[c];
            else
                (*m_columns[m_sourceToNumeric[c]])[static_cast<size_t>(loadedRows)] =
                    parseValue(rowFields[c]);
        }

        for (size_t c = n; c < colCount; ++c)
        {
            if (m_sourceColumnIsString[c])
                (*m_stringColumns[m_sourceToString[c]])[static_cast<size_t>(loadedRows)].clear();
            else
                (*m_columns[m_sourceToNumeric[c]])[static_cast<size_t>(loadedRows)] =
                    std::numeric_limits<double>::quiet_NaN();
        }

        ++loadedRows;
        if (loadedRows % PROGRESS_INTERVAL == 0)
        {
            float p = 0.05f + 0.94f * static_cast<float>(loadedRows) / static_cast<float>(totalDataRows);
            reportProgress(p, "Loading data...",
                           std::to_string(loadedRows) + " / " + std::to_string(totalDataRows));
        }
    }

    m_xAxisColumn = AutoDetectXAxis();

    // 计算每列的全列 min/max 缓存
    for (size_t c = 0; c < m_columns.size(); ++c)
        m_columns[c]->recalcMinMax();

    reportProgress(1.0f, "Done.", "Loaded " + std::to_string(m_columns.size())
                  + " numeric and " + std::to_string(m_stringColumns.size()) + " string columns, " +
                  std::to_string(GetRowCount()) + " rows.");

    return true;
}

// ============================================================
// LoadFromExpr: 表达式加载
// ============================================================

bool DataManager::LoadFromExpr(const std::string& exprStr, const std::string& exprName)
{
    // ---- 空表达式检查 ----
    if (exprStr.empty())
        return false;

    // ---- 列名冲突检查 ----
    if (m_nameIndex.find(exprName) != m_nameIndex.end()
        || m_stringNameIndex.find(exprName) != m_stringNameIndex.end())
        return false;

    // ---- 预设 rowCount（预处理和后续验证都需要） ----
    size_t rowCount = 0;
    if (!m_columns.empty())
        rowCount = m_columns[0]->size();

    // ---- 自定义函数预处理（在提取列名之前） ----
    std::string processedExpr = PreprocessCustomFuncs(exprStr, rowCount);
    if (processedExpr.empty() && CustomFuncRegistry::count() > 0)
    {
        // 预处理失败（参数列不存在等）
    }
    if (!processedExpr.empty())
    {
        // 使用预处理后的表达式
    }
    else
    {
        processedExpr = exprStr;
    }

    // ---- 构建扩展关键字集合（自定义函数名 + 临时列名前缀过滤） ----
    std::unordered_set<std::string> extraKeywords;
    for (uint8_t i = 0; i < CustomFuncRegistry::count(); ++i)
    {
        extraKeywords.insert(std::string(CustomFuncRegistry::entries()[i].name));
    }

    // ---- 解析引用列 ----
    std::vector<std::string> refCols = ParseColumnRefs(processedExpr,
        CustomFuncRegistry::count() > 0 ? &extraKeywords : nullptr);

    // ---- 验证所有引用列存在 ----
    for (const auto& colName : refCols)
    {
        if (m_nameIndex.find(colName) == m_nameIndex.end())
            return false;
    }

    // ---- 验证行数一致 ----
    if (!refCols.empty())
    {
        size_t firstIdx = m_nameIndex[refCols[0]];
        rowCount = m_columns[firstIdx]->size();
        for (size_t i = 1; i < refCols.size(); ++i)
        {
            size_t idx = m_nameIndex[refCols[i]];
            if (m_columns[idx]->size() != rowCount)
                return false;
        }
    }

    // ---- exprtk 设置 ----
    exprtk::symbol_table<double> symbolTable;
    std::vector<double> varValues(refCols.size(), 0.0);

    for (size_t i = 0; i < refCols.size(); ++i)
    {
        symbolTable.add_variable(refCols[i], varValues[i]);
    }

    symbolTable.add_constants();

    exprtk::expression<double> expression;
    expression.register_symbol_table(symbolTable);

    exprtk::parser<double> parser;
    if (!parser.compile(processedExpr, expression))
        return false;

    // ---- 创建结果列 ----
    auto resultCol = std::make_shared<Column>(rowCount);
    resultCol->beginOverwrite();

    for (size_t row = 0; row < rowCount; ++row)
    {
        // 更新引用变量值
        for (size_t i = 0; i < refCols.size(); ++i)
        {
            size_t colIdx = m_nameIndex[refCols[i]];
            varValues[i] = m_columns[colIdx]->getDouble(row);
        }

        (*resultCol)[row] = expression.value();
    }

    // ---- 注册结果列 ----
    size_t newIdx = m_columns.size();
    m_columns.push_back(std::move(resultCol));
    m_columnNames.push_back(exprName);
    m_nameIndex[exprName] = newIdx;
    m_columnMetadata.push_back({ColumnOrigin::ViewerExpression, {}});
    m_rawColumnNames.push_back(exprName);  // 表达式列的 raw name 同 cleaned name

    // 计算表达式列的 min/max 缓存
    m_columns[newIdx]->recalcMinMax();

    return true;
}

// ============================================================
// ParseColumnRefs: 从表达式字符串提取引用的列名
// ============================================================

std::vector<std::string> DataManager::ParseColumnRefs(const std::string& exprStr,
    const std::unordered_set<std::string>* extraKeywords) const
{
    // exprtk 内置关键字（不含用户自定义变量）
    const auto& kKeywords = GetExprtkKeywords();

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    const char* p = exprStr.c_str();
    const char* end = p + exprStr.size();

    while (p < end)
    {
        // 跳过非标识符字符
        while (p < end && !std::isalpha(static_cast<unsigned char>(*p)) && *p != '_')
            ++p;

        if (p >= end)
            break;

        // 提取标识符
        const char* start = p;
        while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
            ++p;

        std::string token(start, p - start);

        if (token.empty())
            continue;

        // 过滤内置关键字
        if (kKeywords.find(token) != kKeywords.end())
            continue;

        // 过滤额外关键字（自定义函数名等）
        if (extraKeywords && extraKeywords->find(token) != extraKeywords->end())
            continue;

        // 检查是否在已有列名中（只报告存在的列）
        if (m_nameIndex.find(token) != m_nameIndex.end() && seen.find(token) == seen.end())
        {
            result.push_back(token);
            seen.insert(token);
        }
    }

    return result;
}

// ============================================================
// PreprocessCustomFuncs: 扫描并展开自定义跨行函数
// ============================================================

std::string DataManager::PreprocessCustomFuncs(const std::string& exprStr, size_t rowCount)
{
    // 确保内置跨行函数已注册
    EnsureDiffFuncsLinked();

    std::string result = exprStr;

    for (uint8_t fi = 0; fi < CustomFuncRegistry::count(); ++fi)
    {
        const auto& entry = CustomFuncRegistry::entries()[fi];
        std::string_view funcName = entry.name;
        std::string searchPrefix = std::string(funcName) + "(";

        size_t searchPos = 0;
        while (true)
        {
            size_t callStart = result.find(searchPrefix, searchPos);
            if (callStart == std::string::npos)
                break;

            // 找到参数列表的起始和结束位置
            size_t argsStart = callStart + searchPrefix.size();
            size_t argsEnd = argsStart;
            int parenDepth = 1;

            while (argsEnd < result.size() && parenDepth > 0)
            {
                if (result[argsEnd] == '(') ++parenDepth;
                else if (result[argsEnd] == ')') --parenDepth;
                if (parenDepth > 0) ++argsEnd;
            }

            if (parenDepth != 0)
            {
                searchPos = callStart + searchPrefix.size();
                continue;
            }

            std::string argsStr = result.substr(argsStart, argsEnd - argsStart);

            // 分割参数（按逗号分隔）
            std::vector<std::string> argNames;
            size_t commaPos = 0;
            while (true)
            {
                while (commaPos < argsStr.size() && argsStr[commaPos] == ' ')
                    ++commaPos;

                size_t nextComma = argsStr.find(',', commaPos);
                std::string arg;
                if (nextComma == std::string::npos)
                {
                    arg = argsStr.substr(commaPos);
                }
                else
                {
                    arg = argsStr.substr(commaPos, nextComma - commaPos);
                }

                // 去除首尾空格
                while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                while (!arg.empty() && arg.back() == ' ') arg.pop_back();

                if (!arg.empty())
                    argNames.push_back(arg);

                if (nextComma == std::string::npos)
                    break;
                commaPos = nextComma + 1;
            }

            // 验证参数个数
            if (argNames.size() != entry.argCount)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 验证所有参数列存在，使用 vector 以适配任意参数个数
            bool allColsExist = true;
            std::vector<Column*> cols;
            cols.reserve(argNames.size());

            for (size_t ai = 0; ai < argNames.size(); ++ai)
            {
                auto it = m_nameIndex.find(argNames[ai]);
                if (it == m_nameIndex.end())
                {
                    allColsExist = false;
                    break;
                }
                cols.push_back(m_columns[it->second].get());
            }

            if (!allColsExist)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 调用 compute 生成临时列
            auto tempCol = entry.compute(cols.data(), static_cast<uint8_t>(cols.size()), rowCount);
            if (!tempCol)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 生成临时列名（避免双下划线和前置下划线，与 exprtk 兼容）
            std::string tempName = "tmp_" + std::string(funcName) + "_";
            for (size_t ai = 0; ai < argNames.size(); ++ai)
            {
                if (ai > 0) tempName += "_";
                tempName += argNames[ai];
            }

            // 确保唯一性
            int suffix = 0;
            std::string uniqueName = tempName;
            while (m_nameIndex.find(uniqueName) != m_nameIndex.end())
            {
                ++suffix;
                uniqueName = tempName + std::to_string(suffix);
            }

            // 注册临时列
            size_t tempIdx = m_columns.size();
            m_columns.push_back(std::shared_ptr<Column>(std::move(tempCol)));
            m_columnNames.push_back(uniqueName);
            m_nameIndex[uniqueName] = tempIdx;
            m_columnMetadata.push_back({ColumnOrigin::ViewerExpression, {}});
            m_rawColumnNames.push_back(uniqueName);

            // 替换表达式中的函数调用为列名
            result.replace(callStart, (argsEnd + 1) - callStart, uniqueName);

            // 从替换位置之后继续搜索
            searchPos = callStart + uniqueName.size();
        }
    }

    return result;
}

// ============================================================
// 索引列维护
// ============================================================

void DataManager::ensureIndexColumnBuilt()
{
    if (m_indexColumn)
        return;

    size_t rowCount = GetRowCount();
    auto idxCol = std::make_unique<Column>(rowCount);
    idxCol->beginOverwrite();
    for (size_t i = 0; i < rowCount; ++i)
        (*idxCol)[i] = static_cast<double>(i);
    idxCol->recalcMinMax();
    m_indexColumn = std::move(idxCol);
}

// ---- 清理 ----

void DataManager::Clear()
{
    m_columns.clear();
    m_columnMetadata.clear();
    m_stringColumns.clear();
    m_stringColumnNames.clear();
    m_rawStringColumnNames.clear();
    m_stringNameIndex.clear();
    m_columnNames.clear();
    m_rawColumnNames.clear();
    m_nameIndex.clear();
    m_sourceColumnNames.clear();
    m_sourceColumnIsString.clear();
    m_sourceToNumeric.clear();
    m_sourceToString.clear();
    m_indexColumn.reset();
    m_filePath.clear();
    m_xAxisColumn = npos;
    m_xAxisUnit = TimeUnit::None;
}

// ---- 列访问 ----

Column* DataManager::GetColumn(size_t idx)
{
    if (idx >= m_columns.size())
        return nullptr;
    return m_columns[idx].get();
}

const Column* DataManager::GetColumn(size_t idx) const
{
    if (idx >= m_columns.size())
        return nullptr;
    return m_columns[idx].get();
}

Column* DataManager::GetColumn(const std::string& name)
{
    size_t idx = GetColumnIndex(name);
    if (idx == npos) return nullptr;
    return GetColumn(idx);
}

const Column* DataManager::GetColumn(const std::string& name) const
{
    size_t idx = GetColumnIndex(name);
    if (idx == npos) return nullptr;
    return GetColumn(idx);
}

std::shared_ptr<const Column> DataManager::GetColumnShared(size_t idx) const
{
    if (idx >= m_columns.size())
        return {};
    return m_columns[idx];
}

size_t DataManager::GetColumnIndex(const std::string& name) const
{
    auto it = m_nameIndex.find(name);
    if (it != m_nameIndex.end())
        return it->second;
    return npos;
}

const StringColumn* DataManager::GetStringColumn(size_t idx) const
{
    if (idx >= m_stringColumns.size())
        return nullptr;
    return m_stringColumns[idx].get();
}

const StringColumn* DataManager::GetStringColumn(const std::string& name) const
{
    const size_t idx = GetStringColumnIndex(name);
    return idx == npos ? nullptr : GetStringColumn(idx);
}

size_t DataManager::GetStringColumnIndex(const std::string& name) const
{
    const auto it = m_stringNameIndex.find(name);
    return it == m_stringNameIndex.end() ? npos : it->second;
}

std::string DataManager::GetStringValue(size_t colIdx, size_t rowIdx) const
{
    const StringColumn* column = GetStringColumn(colIdx);
    if (!column || rowIdx >= column->size())
        return {};
    return (*column)[rowIdx];
}

std::string DataManager::GetStringValue(const std::string& colName, size_t rowIdx) const
{
    const size_t index = GetStringColumnIndex(colName);
    return index == npos ? std::string{} : GetStringValue(index, rowIdx);
}

size_t DataManager::GetRowCount() const noexcept
{
    if (!m_columns.empty())
        return m_columns[0]->size();
    return m_stringColumns.empty() ? 0 : m_stringColumns[0]->size();
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

// ---- 横轴检测 ----

size_t DataManager::AutoDetectXAxis()
{
    if (m_columnNames.empty())
        return npos;

    // ---- 规则匹配（优先使用 user/xaxis.json 中的规则） ----
    if (!m_xAxisRules.empty())
    {
        for (const auto& rule : m_xAxisRules)
        {
            // 构建小写 pattern
            std::string lowerPattern;
            lowerPattern.reserve(rule.pattern.size());
            for (char c : rule.pattern)
                lowerPattern += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            for (size_t i = 0; i < m_columnNames.size(); ++i)
            {
                std::string lowerName;
                lowerName.reserve(m_columnNames[i].size());
                for (char c : m_columnNames[i])
                    lowerName += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                if (lowerName.find(lowerPattern) != std::string::npos)
                {
                    // 匹配成功：设置单位并返回
                    m_xAxisUnit = rule.unit;
                    return i;
                }
            }
        }
    }

    // ---- 回退：内置硬编码关键词匹配 ----
    static const std::vector<std::pair<std::string, TimeUnit>> keywordRules =
    {
        {"time_s",    TimeUnit::Second},
        {"time_ms",   TimeUnit::Millisecond},
        {"time_us",   TimeUnit::Microsecond},
        {"time_ns",   TimeUnit::Nanosecond},
        {"time_min",  TimeUnit::Minute},
        {"time_h",    TimeUnit::Hour},
        {"timestamp", TimeUnit::Second},
        {"datetime",  TimeUnit::Day},
        {"date",      TimeUnit::Day},
        {"time",      TimeUnit::Second},
        {"utc",       TimeUnit::Second},
        {"epoch",     TimeUnit::Second},
        {"t",         TimeUnit::Second},
        {"x",         TimeUnit::None},
        {"index",     TimeUnit::None},
        {"id",        TimeUnit::None},
    };

    // 对每列名计算匹配得分
    struct Match { size_t index; int score; TimeUnit unit; };
    std::vector<Match> matches;

    for (size_t i = 0; i < m_columnNames.size(); ++i)
    {
        const std::string& name = m_columnNames[i];

        std::string lowerName;
        lowerName.reserve(name.size());
        for (char c : name)
            lowerName += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        int score = 0;
        TimeUnit bestUnit = TimeUnit::None;
        for (const auto& [kw, unit] : keywordRules)
        {
            if (lowerName == kw)
            {
                score += 100;
                bestUnit = unit;
            }
            else if (lowerName.find(kw) != std::string::npos && score < 50)
            {
                score = 50;
                bestUnit = unit;
            }
        }
        matches.push_back({i, score, bestUnit});
    }

    size_t bestIdx = npos;
    int bestScore = 0;
    for (const auto& m : matches)
    {
        if (m.score > bestScore)
        {
            bestScore = m.score;
            bestIdx = m.index;
            m_xAxisUnit = m.unit;
        }
    }

    return bestIdx;
}

// ============================================================
// X 轴规则 JSON 读写
// ============================================================

bool DataManager::LoadXAxisRules(const std::string& jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // 简易 JSON 解析（避免引入第三方库依赖）
    m_xAxisRules.clear();

    // 查找数组起始
    size_t pos = content.find('[');
    if (pos == std::string::npos)
        return false;
    ++pos;

    // 逐对象解析
    while (pos < content.size())
    {
        // 跳过空白和逗号
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'
               || content[pos] == '\n' || content[pos] == '\r' || content[pos] == ','))
            ++pos;

        if (pos >= content.size() || content[pos] == ']')
            break;

        // 查找对象起始 {
        if (content[pos] != '{')
        {
            ++pos;
            continue;
        }
        ++pos;

        std::string pattern;
        TimeUnit unit = TimeUnit::None;

        // 解析对象内的键值对
        while (pos < content.size() && content[pos] != '}')
        {
            // 跳过空白
            while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'
                   || content[pos] == '\n' || content[pos] == '\r' || content[pos] == ','))
                ++pos;

            if (pos >= content.size() || content[pos] == '}')
                break;

            // 读取键（引号内）
            if (content[pos] != '"')
            {
                ++pos;
                continue;
            }
            ++pos;
            size_t keyEnd = content.find('"', pos);
            if (keyEnd == std::string::npos) break;
            std::string key = content.substr(pos, keyEnd - pos);
            pos = keyEnd + 1;

            // 跳过冒号
            while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t'
                   || content[pos] == '\n' || content[pos] == '\r' || content[pos] == ':'))
                ++pos;

            if (pos >= content.size()) break;

            // 读取值
            if (content[pos] == '"')
            {
                // 字符串值
                ++pos;
                size_t valEnd = content.find('"', pos);
                if (valEnd == std::string::npos) break;
                std::string val = content.substr(pos, valEnd - pos);
                pos = valEnd + 1;
                if (key == "pattern")
                    pattern = val;
                else if (key == "unit")
                    unit = timeUnitFromLabel(val);
            }
            else if (content[pos] == '-' || (content[pos] >= '0' && content[pos] <= '9'))
            {
                // 数值值
                size_t valEnd = pos;
                while (valEnd < content.size() && ((content[valEnd] >= '0' && content[valEnd] <= '9') || content[valEnd] == '-'))
                    ++valEnd;
                int val = std::stoi(content.substr(pos, valEnd - pos));
                pos = valEnd;
                if (key == "unit")
                    unit = static_cast<TimeUnit>(val);
            }
            else
            {
                ++pos;
            }
        }

        // 跳过闭合 }
        if (pos < content.size() && content[pos] == '}')
            ++pos;

        if (!pattern.empty())
            m_xAxisRules.push_back({pattern, unit});
    }

    return true;
}

bool DataManager::SaveXAxisRules(const std::string& jsonPath) const
{
    // 确保目录存在
    std::filesystem::path filePath(jsonPath);
    auto parentDir = filePath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        std::filesystem::create_directories(parentDir);

    std::ofstream file(jsonPath);
    if (!file.is_open())
        return false;

    file << "[\n";
    for (size_t i = 0; i < m_xAxisRules.size(); ++i)
    {
        file << "    {\"pattern\": \"" << m_xAxisRules[i].pattern
             << "\", \"unit\": " << static_cast<int>(m_xAxisRules[i].unit) << "}";
        if (i + 1 < m_xAxisRules.size())
            file << ",";
        file << "\n";
    }
    file << "]\n";
    file.close();
    return true;
}

} // namespace viewer
