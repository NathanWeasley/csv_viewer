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

namespace viewer
{

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

    // 总列数
    size_t GetColumnCount() const noexcept { return m_columns.size(); }

    // 总行数（所有列应相同，取第 0 列的行数）
    size_t GetRowCount() const noexcept;

    // 列名列表（清洗后的）
    const std::vector<std::string>& GetColumnNames() const noexcept { return m_columnNames; }

    // 原始列名列表（清洗前，来自 CSV 表头）
    const std::vector<std::string>& GetRawColumnNames() const noexcept { return m_rawColumnNames; }

    // 按列名查找索引（未找到返回 npos）
    size_t GetColumnIndex(const std::string& name) const;

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

    // 自动检测最适合作为 X 轴的列（匹配时间戳/时间相关列名）
    // 返回列索引，未找到返回 npos
    size_t AutoDetectXAxis() const;

    // 获取当前横轴列索引
    size_t GetXAxisColumn() const noexcept { return m_xAxisColumn; }

    // 设置横轴列
    void SetXAxisColumn(size_t colIdx) { m_xAxisColumn = colIdx; }

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
    std::vector<std::unique_ptr<Column>> m_columns;

    std::vector<std::string> m_columnNames;       // 清洗后的列名
    std::vector<std::string> m_rawColumnNames;    // 原始列名
    std::unordered_map<std::string, size_t> m_nameIndex;  // 列名->索引

    std::string m_filePath;     // 已加载的文件路径
    size_t      m_xAxisColumn = npos;  // 当前横轴列

    // 隐含索引列（0.0, 1.0, 2.0, ...），不加入 m_columns/m_columnNames
    std::unique_ptr<Column> m_indexColumn;

    // 别名映射（原始列名 → 重命名），由 UI 层设置
    std::unordered_map<std::string, std::string> m_aliasMap;

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