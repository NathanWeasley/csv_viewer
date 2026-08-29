#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include "code_viewer/datamgr/custom_expr/custom_func.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace viewer
{

// ============================================================
// X轴时间单位（编译期常量枚举 + constexpr 映射）
// ============================================================
enum class TimeUnit : uint8_t
{
    None = 0,       // 无单位 / 数据索引
    Second,         // s
    Millisecond,    // ms
    Microsecond,    // us
    Nanosecond,     // ns
    Minute,         // min
    Hour,           // h
    Day,            // day
    Count           // 哨兵值，等于有效枚举数
};

// constexpr 转换因子 → 秒
constexpr double timeUnitToSeconds(TimeUnit u) noexcept
{
    switch (u)
    {
        case TimeUnit::Second:      return 1.0;
        case TimeUnit::Millisecond: return 1e-3;
        case TimeUnit::Microsecond: return 1e-6;
        case TimeUnit::Nanosecond:  return 1e-9;
        case TimeUnit::Minute:      return 60.0;
        case TimeUnit::Hour:        return 3600.0;
        case TimeUnit::Day:         return 86400.0;
        default:                    return 1.0;
    }
}

// constexpr 显示标签（仅用于 UI 下拉框填充）
constexpr const char* timeUnitLabel(TimeUnit u) noexcept
{
    switch (u)
    {
        case TimeUnit::None:        return "\u65e0\u5355\u4f4d";   // 无单位
        case TimeUnit::Second:      return "s";
        case TimeUnit::Millisecond: return "ms";
        case TimeUnit::Microsecond: return "\u03bcs";              // μs
        case TimeUnit::Nanosecond:  return "ns";
        case TimeUnit::Minute:      return "min";
        case TimeUnit::Hour:        return "h";
        case TimeUnit::Day:         return "day";
        default:                    return "?";
    }
}

// 所有单位标签数组（用于 UI 填充下拉框）
constexpr const char* g_timeUnitLabels[] = {
    "\u65e0\u5355\u4f4d", "s", "ms", "\u03bcs", "ns", "min", "h", "day"
};
constexpr size_t g_timeUnitLabelCount = sizeof(g_timeUnitLabels) / sizeof(g_timeUnitLabels[0]);

// 从字符串标签查找 TimeUnit 枚举值（用于 JSON 反序列化）
inline TimeUnit timeUnitFromLabel(const std::string& label)
{
    for (size_t i = 0; i < g_timeUnitLabelCount; ++i)
    {
        if (label == g_timeUnitLabels[i])
            return static_cast<TimeUnit>(i);
    }
    return TimeUnit::None;
}

// ============================================================
// X轴匹配规则
// ============================================================
struct XAxisRule
{
    std::string pattern;    // 列名匹配模式（子串，不区分大小写）
    TimeUnit    unit;       // 时间单位（编译期枚举）
};

// ============================================================
// 进度回调类型
// ============================================================
using ProgressCallback = std::function<void(
    float progress,             // 0.0 ~ 1.0
    const std::string& stage,   // 当前阶段描述
    const std::string& detail   // 细节信息（如当前行号）
)>;

// ============================================================
// 加载配置
// ============================================================
struct LoadConfig
{
    std::filesystem::path filePath; // CSV 文件路径 (UTF-8/Unicode safe)
    int         headerRow = 0;      // 表头所在行（0-based）
    bool        hasHeader = true;   // 是否有表头
    char        delimiter = ',';    // 分隔符
    char        quoteChar = '"';    // 转义字符
    ProgressCallback progressCb;    // 进度回调

    // Optional: pre-sanitized column names (if empty, auto-generated from header)
    std::vector<std::string> preSanitizedNames;
};

// 前向声明
class CsvRowReader;

enum class AddDerivedColumnStatus
{
    Success,
    InvalidName,
    DuplicateName,
    RowCountMismatch
};

enum class ColumnOrigin
{
    Loaded,
    ViewerExpression,
    PluginDerived
};

struct ColumnMetadata
{
    ColumnOrigin origin = ColumnOrigin::Loaded;
    std::string ownerPluginId;
};

enum class PublishDerivedColumnsStatus
{
    Success,
    InvalidProvider,
    InvalidName,
    NameCollision,
    RowCountMismatch
};

// ============================================================
// DataManager: CSV 数据管理器
// ============================================================
class VIEWER_API DataManager
{
public:
    DataManager() = default;
    ~DataManager() = default;

    // ---- 禁止拷贝，允许移动 ----
    DataManager(const DataManager&) = delete;
    DataManager& operator=(const DataManager&) = delete;
    DataManager(DataManager&&) noexcept = default;
    DataManager& operator=(DataManager&&) noexcept = default;

    // ============================================================
    // 核心加载接口
    // ============================================================
    bool LoadFromCSV(const LoadConfig& config);

    // Replace the current dataset with already-decoded numeric columns.
    // All columns must have the same row count.
    bool LoadFromColumns(std::vector<std::string> columnNames,
                         std::vector<std::vector<double>> values,
                         const std::string& sourcePath);

    bool LoadFromColumns(std::vector<std::string> columnNames,
                         std::vector<std::vector<double>> values,
                         std::vector<std::string> stringColumnNames,
                         std::vector<std::vector<std::string>> stringValues,
                         const std::string& sourcePath);

    // Adds one plugin-produced column without copying its numeric buffer.
    // Existing columns remain valid because column storage is shared.
    AddDerivedColumnStatus AddDerivedColumn(std::string name,
                                            std::vector<double>&& values);

    PublishDerivedColumnsStatus PublishPluginDerivedColumns(
        const std::string& ownerPluginId,
        std::vector<std::string> names,
        std::vector<std::vector<double>> values,
        std::vector<std::shared_ptr<Column>>* removedColumns = nullptr);

    // ============================================================
    // 表达式加载接口
    // ============================================================
    bool LoadFromExpr(const std::string& exprStr, const std::string& exprName);

    // ============================================================
    // 别名设置
    // ============================================================
    void SetAliasMap(const std::unordered_map<std::string, std::string>& aliasMap)
    { m_aliasMap = aliasMap; }
    const std::unordered_map<std::string, std::string>& GetAliasMap() const noexcept
    { return m_aliasMap; }

    // ============================================================
    // 列访问接口
    // ============================================================

    // 通过列索引获取列
    Column* GetColumn(size_t idx);
    const Column* GetColumn(size_t idx) const;

    // 通过列名获取列
    Column* GetColumn(const std::string& name);
    const Column* GetColumn(const std::string& name) const;

    // Stable ownership used by plugin data snapshots. A snapshot keeps the
    // column alive even after another dataset is loaded.
    std::shared_ptr<const Column> GetColumnShared(size_t idx) const;

    // 总列数
    size_t GetColumnCount() const noexcept { return m_columns.size(); }

    // 总行数（所有列应相同，取第 0 列的行数）
    size_t GetRowCount() const noexcept;

    // 列名列表（清洗后的）
    const std::vector<std::string>& GetColumnNames() const noexcept { return m_columnNames; }

    // 原始列名列表（清洗前，来自 CSV 表头）
    const std::vector<std::string>& GetRawColumnNames() const noexcept { return m_rawColumnNames; }

    const ColumnMetadata* GetColumnMetadata(size_t index) const noexcept
    {
        return index < m_columnMetadata.size() ? &m_columnMetadata[index] : nullptr;
    }

    // 按列名查找索引（未找到返回 npos）
    size_t GetColumnIndex(const std::string& name) const;

    // 字符串列与数值列分别存储；字符串列不参与绘图和表达式计算。
    const StringColumn* GetStringColumn(size_t idx) const;
    const StringColumn* GetStringColumn(const std::string& name) const;
    size_t GetStringColumnCount() const noexcept { return m_stringColumns.size(); }
    size_t GetStringColumnIndex(const std::string& name) const;
    const std::vector<std::string>& GetStringColumnNames() const noexcept
    { return m_stringColumnNames; }
    std::string GetStringValue(size_t colIdx, size_t rowIdx) const;
    std::string GetStringValue(const std::string& colName, size_t rowIdx) const;

    // 日期轴名称按清洗后的源列名匹配；Clear 只清数据，不清该配置。
    void SetDateAxisName(std::string name) { m_dateAxisName = std::move(name); }
    const std::string& GetDateAxisName() const noexcept { return m_dateAxisName; }
    size_t GetDateAxisColumn() const { return GetStringColumnIndex(m_dateAxisName); }
    std::string GetDateValue(size_t rowIdx) const
    { return GetStringValue(m_dateAxisName, rowIdx); }

    // ============================================================
    // 行访问接口
    // ============================================================

    // 获取指定行所有列的值（double 形式）
    std::vector<double> GetRowAsDoubles(size_t rowIdx) const;

    // 获取指定行指定列的值
    double GetValueAsDouble(size_t colIdx, size_t rowIdx) const;
    double GetValueAsDouble(const std::string& colName, size_t rowIdx) const;

    // ============================================================
    // 横轴检测
    // ============================================================

    // 基于规则检测最适合作为 X 轴的列（优先使用 xaxis.json 规则）
    // 返回列索引，未找到返回 npos。会同时设置 m_xAxisUnit
    size_t AutoDetectXAxis();

    // 获取当前横轴列索引
    size_t GetXAxisColumn() const noexcept { return m_xAxisColumn; }

    // 设置横轴列
    void SetXAxisColumn(size_t colIdx) { m_xAxisColumn = colIdx; }

    // ============================================================
    // X 轴规则管理
    // ============================================================

    // 加载 X 轴检测规则（从 JSON 文件）
    bool LoadXAxisRules(const std::string& jsonPath);

    // 保存 X 轴检测规则到 JSON 文件
    bool SaveXAxisRules(const std::string& jsonPath) const;

    // 获取当前规则列表
    const std::vector<XAxisRule>& GetXAxisRules() const noexcept { return m_xAxisRules; }

    // 设置规则列表（并保存）
    void SetXAxisRules(const std::vector<XAxisRule>& rules) { m_xAxisRules = rules; }

    // 获取当前 X 轴单位
    TimeUnit GetXAxisUnit() const noexcept { return m_xAxisUnit; }

    // ============================================================
    // 隐含索引列（内部使用，不暴露在 UI 数据树中）
    // ============================================================

    // 构建隐含索引列（0.0, 1.0, 2.0, ...），幂等
    void ensureIndexColumnBuilt();

    // 获取隐含索引列指针，在 ensureIndexColumnBuilt() 之后有效
    const Column* GetIndexColumn() const noexcept { return m_indexColumn.get(); }

    // ============================================================
    // 工具
    // ============================================================

    // 清理所有数据
    void Clear();

    // 文件路径
    const std::string& GetFilePath() const noexcept { return m_filePath; }

private:
    // ---- 表达式列名提取 ----
    // extraKeywords: 额外的排除标识符（如自定义函数名），不在其中且存在于 m_nameIndex 中的才会被返回
    std::vector<std::string> ParseColumnRefs(const std::string& exprStr,
        const std::unordered_set<std::string>* extraKeywords = nullptr) const;

    // ---- 自定义函数预处理 ----
    // 扫描 exprStr 中的自定义跨行函数调用，预计算临时列并替换表达式文本
    // 返回处理后的表达式字符串。若某函数调用参数列不存在则返回空字符串表示失败
    std::string PreprocessCustomFuncs(const std::string& exprStr, size_t rowCount);

    // ---- 内部数据 ----
    std::vector<std::shared_ptr<Column>> m_columns;
    std::vector<ColumnMetadata> m_columnMetadata;

    std::vector<std::shared_ptr<StringColumn>> m_stringColumns;
    std::vector<std::string> m_stringColumnNames;
    std::vector<std::string> m_rawStringColumnNames;
    std::unordered_map<std::string, size_t> m_stringNameIndex;

    std::vector<std::string> m_columnNames;       // 清洗后的列名
    std::vector<std::string> m_rawColumnNames;    // 原始列名
    std::unordered_map<std::string, size_t> m_nameIndex;  // 列名->索引

    // 完整源表结构用于在数值列/字符串列分离后继续追加 CSV。
    std::vector<std::string> m_sourceColumnNames;
    std::vector<bool> m_sourceColumnIsString;
    std::vector<size_t> m_sourceToNumeric;
    std::vector<size_t> m_sourceToString;

    std::string m_filePath;     // 已加载的文件路径
    size_t      m_xAxisColumn = npos;  // 当前横轴列
    TimeUnit    m_xAxisUnit = TimeUnit::None;   // 当前横轴单位

    // X 轴检测规则（从 user/xaxis.json 加载）
    std::vector<XAxisRule> m_xAxisRules;

    // 隐含索引列（0.0, 1.0, 2.0, ...），不加入 m_columns/m_columnNames
    std::unique_ptr<Column> m_indexColumn;

    // 别名映射（原始列名 → 重命名），由 UI 层设置
    std::unordered_map<std::string, std::string> m_aliasMap;
    std::string m_dateAxisName;

    static constexpr size_t npos = static_cast<size_t>(-1);

    friend class CsvRowReader;
};

// ============================================================
// CsvRowReader: 轻量级逐行 CSV 解析器
// 仅读取一行数据到内存，不累积
// ============================================================
class CsvRowReader
{
public:
    CsvRowReader(const std::filesystem::path& filePath, char delimiter = ',', char quote = '"');

    // 打开文件
    bool open();

    // 读取下一行，返回 true 表示成功读取一行
    // fields 输出为当前行的各字段字符串
    bool readRow(std::vector<std::string>& fields);

    // 获取当前行号（0-based）
    size_t lineNumber() const noexcept { return m_lineNum; }

    // 获取总行数预估（用于进度条）
    // 注意：仅通过第一次遍历计数获得
    size_t totalLines() const noexcept { return m_totalLines; }

    // 关闭
    void close();

    // 重置到文件开头
    bool reset();

private:
    // 解析一个 CSV 行（处理引号转义）
    bool parseLine(const std::string& line, std::vector<std::string>& fields);

    std::filesystem::path m_filePath;
    char m_delimiter;
    char m_quote;
    std::ifstream m_stream;
    size_t m_lineNum = 0;
    size_t m_totalLines = 0;
};

} // namespace viewer
