#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace viewer
{

// 前向声明
class DataManager;

// ============================================================
// PlotExpression: 图窗内临时表达式（独立内存拷贝，不影响 DataManager）
// ============================================================
struct PlotExpression
{
    std::string expressionText;                   // 当前表达式文本，初始值 = 数据项名称
    std::unique_ptr<Column<double>> computedData; // 独立的计算数据拷贝
    bool isEdited = false;                        // 是否被用户编辑过（表达式文本 != 数据项名称）
};

// ============================================================
// ExprManager: 单图窗表达式管理器（纯 C++，无 Qt 依赖）
//
// 职责：
//   - 维护当前图窗内每个数据项的临时表达式
//   - 表达式初始值 = 数据项名称（未编辑时直接拷贝原始数据）
//   - 支持 exprtk 编译检查与计算
//   - 通过 std::function 回调通知 UI 层
//
// 每个 PlotPageInfo 持有一个 ExprManager 实例
// ============================================================
class VIEWER_API ExprManager
{
public:
    ExprManager() = default;
    ~ExprManager() = default;

    // ---- 禁止拷贝，允许移动 ----
    ExprManager(const ExprManager&) = delete;
    ExprManager& operator=(const ExprManager&) = delete;
    ExprManager(ExprManager&&) noexcept = default;
    ExprManager& operator=(ExprManager&&) noexcept = default;

    // ============================================================
    // 表达式访问
    // ============================================================

    // 获取或创建指定数据项的表达式（若不存在则创建并深拷贝原始列数据）
    PlotExpression& getOrCreate(const std::string& itemName, DataManager& dm);

    // 仅获取表达式指针（不创建），不存在返回 nullptr
    PlotExpression* get(const std::string& itemName);

    // 检查指定数据项是否有表达式
    bool has(const std::string& itemName) const;

    // ============================================================
    // 表达式编辑
    // ============================================================

    // 设置表达式文本
    void setExpressionText(const std::string& itemName, const std::string& text);

    // 根据表达式文本重新计算 computedData（使用 exprtk）
    // dm 提供原始列数据引用
    // 返回 true 表示计算成功，false 表示编译失败
    bool recompute(const std::string& itemName, DataManager& dm);

    // 仅校验表达式合法性（不执行计算）
    // 返回 true 表示表达式可以编译通过
    // 注意：非 const 是因为需要调用自定义函数预处理
    bool validate(const std::string& exprStr, DataManager& dm);

    // ============================================================
    // 数据拷贝
    // ============================================================

    // 深拷贝一个表达式的 computedData（用于复制图窗）
    PlotExpression copy(const std::string& itemName) const;

    // 深拷贝整个表达式表（用于复制图窗）
    std::unordered_map<std::string, PlotExpression> copyAll() const;

    // 写入表达式表（用于复制图窗时注入）
    void insertAll(std::unordered_map<std::string, PlotExpression>&& exprs);

    // ============================================================
    // 生命周期
    // ============================================================

    // 移除一个数据项的表达式
    void removeItem(const std::string& itemName);

    // 清空所有表达式
    void clearAll();

    // ============================================================
    // 回调（UI 层绑定）
    // ============================================================

    // 表达式重新计算后触发，参数为数据项名称
    // UI 层需同步更新图窗显示
    std::function<void(const std::string& itemName)> onExpressionRecomputed;

    // 表达式文本变更后触发，参数为 (数据项名称, 表达式文本)
    // UI 层用于同步工具栏星号等
    std::function<void(const std::string& itemName, const std::string& exprText)> onExpressionTextChanged;

private:
    // 从表达式字符串中提取引用的列名（在 DataManager 中存在的列）
    std::vector<std::string> extractRefColumns(const std::string& exprStr, DataManager& dm) const;

    // 预处理自定义跨行函数（fdiff, bdiff 等），将其替换为临时列名
    // 返回预处理后的表达式字符串；若参数列不存在则返回空字符串表示失败
    std::string preprocessCustomFuncs(const std::string& exprStr, DataManager& dm, size_t rowCount);

    // 构建需要从关键字中排除的自定义函数名集合（用于 extractRefColumns）
    std::unordered_set<std::string> customFuncNames() const;

    // 数据项名称 → 表达式
    std::unordered_map<std::string, PlotExpression> m_exprs;

    // 自定义函数预处理生成的临时列（生命周期与 ExprManager 一致）
    // 在 recompute 中可能被覆盖，旧列自动释放
    std::unordered_map<std::string, std::unique_ptr<Column<double>>> m_tempCols;
};

} // namespace viewer