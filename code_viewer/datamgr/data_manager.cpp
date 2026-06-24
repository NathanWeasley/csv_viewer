#include "code_viewer/datamgr/data_manager.h"
#include "code_viewer/datamgr/custom_expr/diff_func.h"
#include "code_exprtk/exprtk.hpp"
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
    const bool isFirstLoad = m_columns.empty();

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
    {
        std::vector<std::string> tmpFields;
        while (counter.readRow(tmpFields))
            ++totalDataRows;
    }
    counter.close();

    if (config.hasHeader && totalDataRows > 0)
    {
        if (static_cast<uint64_t>(config.headerRow) < totalDataRows)
            totalDataRows -= 1;
    }

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

    // ================================================================
    // 列名校验（后续 CSV 加载时）
    // ================================================================
    std::vector<std::string> newSanitizedNames;
    std::unordered_map<std::string, size_t> newNameIndex;

    if (!config.preSanitizedNames.empty())
    {
        newSanitizedNames = config.preSanitizedNames;
        for (size_t i = 0; i < newSanitizedNames.size(); ++i)
            newNameIndex[newSanitizedNames[i]] = i;
    }
    else
    {
        std::vector<std::string> rawNames;
        if (config.hasHeader && !headerFields.empty())
            rawNames = headerFields;
        else
        {
            rawNames.resize(colCount);
            for (size_t c = 0; c < colCount; ++c)
                rawNames[c] = "Col_" + std::to_string(c);
        }
        sanitizeColumnNames(rawNames, newSanitizedNames, newNameIndex);
    }

    if (!isFirstLoad)
    {
        // ---- 后续 CSV: 校验列数 & 列名 ----
        if (colCount != m_columns.size())
            return false;

        for (size_t i = 0; i < m_columnNames.size(); ++i)
        {
            if (i >= newSanitizedNames.size())
                return false;
            if (newSanitizedNames[i] != m_columnNames[i])
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

        while (scanner.readRow(rowFields))
        {
            size_t n = rowFields.size();
            if (n != colCount)
                return false;

            for (size_t c = 0; c < colCount; ++c)
            {
                m_columns[c]->pushFromString(rowFields[c]);
            }

            ++appendedRows;
            if (appendedRows % PROGRESS_INTERVAL == 0)
            {
                float p = 0.05f + 0.90f * static_cast<float>(appendedRows) / static_cast<float>(totalDataRows);
                reportProgress(p, "Appending data...",
                               std::to_string(appendedRows) + " / " + std::to_string(totalDataRows));
            }
        }

        reportProgress(1.0f, "Done.", "Appended " + std::to_string(totalDataRows) + " rows.");

        return true;
    }

    // ================================================================
    // 首个 CSV 加载: 两遍扫描
    // ================================================================

    m_filePath = config.filePath.string();
    m_columnNames = newSanitizedNames;
    m_nameIndex = newNameIndex;

    if (config.hasHeader && !headerFields.empty())
        m_rawColumnNames = headerFields;
    else
    {
        m_rawColumnNames.resize(colCount);
        for (size_t c = 0; c < colCount; ++c)
            m_rawColumnNames[c] = "col_" + std::to_string(c + 1);
    }

    // ---- 阶段 2a: 扫描所有行的类型 ----
    reportProgress(0.05f, "Scanning data types...", "");

    std::vector<TypeCount> typeCounters(colCount);

    uint64_t scannedRows = 0;
    std::vector<std::string> rowFields;

    scanner.reset();
    if (config.hasHeader)
    {
        for (int i = 0; i <= config.headerRow; ++i)
            scanner.readRow(rowFields);
    }
    rowFields.clear();

    const uint64_t PROGRESS_INTERVAL = std::max<uint64_t>(1, totalDataRows / 200);

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

    // ---- 阶段 2b: 第二遍读取 - 数据写入 ----
    reportProgress(0.52f, "Loading data (pass 2/2)...", "");

    m_columns.resize(colCount);
    for (size_t c = 0; c < colCount; ++c)
    {
        if (colTypes[c] == ColumnType::Int64)
            m_columns[c] = std::make_unique<Column<int64_t>>(ColumnType::Int64);
        else
            m_columns[c] = std::make_unique<Column<double>>(ColumnType::Float64);
    }

    CsvRowReader writer(config.filePath, config.delimiter, config.quoteChar);
    if (!writer.open())
    {
        Clear();
        return false;
    }

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

    m_xAxisColumn = AutoDetectXAxis();

    // 重建所有列的 chunk 元数据（降采样加速用）
    for (auto& col : m_columns)
        col->rebuildAllChunkMeta();

    reportProgress(1.0f, "Done.", "Loaded " + std::to_string(colCount) + " columns, " +
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
    if (m_nameIndex.find(exprName) != m_nameIndex.end())
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
        // 注意：如果没有任何自定义函数注册，processedExpr 可能为空是正常的
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
    // 将自定义函数名加入排除列表，防止被 ParseColumnRefs 当作列名
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
    auto resultCol = std::make_unique<Column<double>>(ColumnType::Float64);

    for (size_t row = 0; row < rowCount; ++row)
    {
        // 更新引用变量值
        for (size_t i = 0; i < refCols.size(); ++i)
        {
            size_t colIdx = m_nameIndex[refCols[i]];
            varValues[i] = m_columns[colIdx]->getDouble(row);
        }

        double val = expression.value();
        resultCol->push_back(val);
    }

    // ---- 注册结果列 ----
    size_t newIdx = m_columns.size();
    m_columns.push_back(std::move(resultCol));
    m_columnNames.push_back(exprName);
    m_nameIndex[exprName] = newIdx;
    m_rawColumnNames.push_back(exprName);  // 表达式列的 raw name 同 cleaned name

    return true;
}

// ============================================================
// ParseColumnRefs: 从表达式字符串提取引用的列名
// ============================================================

std::vector<std::string> DataManager::ParseColumnRefs(const std::string& exprStr,
    const std::unordered_set<std::string>* extraKeywords) const
{
    // exprtk 内置关键字（不含用户自定义变量）
    static const std::unordered_set<std::string> kKeywords =
    {
        // 数学函数
        "sin", "cos", "tan", "abs", "acos", "asin", "atan", "atan2",
        "ceil", "floor", "round", "trunc", "frac", "sgn",
        "cosh", "sinh", "tanh", "acosh", "asinh", "atanh",
        "exp", "expm1", "log", "log10", "log1p", "log2",
        "sqrt", "cbrt", "pow", "hypot",
        "min", "max", "clamp", "inrange",
        "deg2rad", "rad2deg",
        "cot", "csc", "sec", "acot", "acsc", "asec",
        "coth", "csch", "sech", "acoth", "acsch", "asech",
        "mod", "erf", "erfc", "ncdf",

        // 控制流
        "if", "switch", "case", "default",
        "while", "repeat", "until",
        "var", "return",

        // 逻辑
        "and", "nand", "or", "nor", "xor", "xnor", "not",
        "mand", "mor",

        // 常量
        "pi", "epsilon", "inf", "nan",
        "true", "false",

        // 保留
        "break", "continue", "for",
        "e",  // exprtk's 'e' constant
    };

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

        // 跳过数字开头的（如 1e5 中的 e5 部分已被 e 单独拦截，
        // 但纯数字开头的不是合法标识符，已在上面被 isalpha 跳过）
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
            std::vector<AbstractColumn*> cols;
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
            m_columns.push_back(std::move(tempCol));
            m_columnNames.push_back(uniqueName);
            m_nameIndex[uniqueName] = tempIdx;
            m_rawColumnNames.push_back(uniqueName);

            // 替换表达式中的函数调用为列名
            result.replace(callStart, (argsEnd + 1) - callStart, uniqueName);

            // 从替换位置之后继续搜索
            searchPos = callStart + uniqueName.size();
        }
    }

    return result;
}

// ---- 清理 ----

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
    static const std::vector<std::string> keywords =
    {
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