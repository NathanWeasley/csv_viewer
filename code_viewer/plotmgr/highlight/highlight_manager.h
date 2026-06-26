#pragma once

#include "code_viewer/base/base_def.h"
#include <QColor>
#include <functional>
#include <string>
#include <vector>

namespace viewer
{

// 前向声明
class DataManager;

// ============================================================
// HighlightCondition: 比较条件枚举
// ============================================================
enum class HighlightCondition
{
    Greater,   // >
    Less,      // <
    Equal,     // ==
    NotEqual,  // !=
    Between    // 介于
};

// ============================================================
// HighlightRule: 单条高亮规则
// ============================================================
struct HighlightRule
{
    std::string dataColumn;               // 数据列名
    HighlightCondition condition = HighlightCondition::Greater;  // 比较条件
    double value1 = 0.0;                  // 阈值1（Between 时为下界）
    double value2 = 0.0;                  // 阈值2（Between 时有效）
    QColor  color = QColor(255, 255, 0);  // 填充颜色（默认黄色）
    int     alpha = 100;                  // 透明度 0~255
    std::string label;                    // 文字标注

    // 条件部分相同即视为同一条规则（用于去重）
    // 比较：列名 + 条件 + 阈值1 + 阈值2
    bool operator==(const HighlightRule& other) const
    {
        return dataColumn == other.dataColumn
            && condition == other.condition
            && value1 == other.value1
            && value2 == other.value2;
    }
};

// ============================================================
// HighlightInterval: 计算后的高亮区间
// ============================================================
struct HighlightInterval
{
    double xStart = 0.0;
    double xEnd = 0.0;
    QColor color;
    int alpha = 100;
    std::string label;
};

// ============================================================
// HighlightManager: 单图窗高亮规则管理器（纯 C++，无 Qt 依赖）
//
// 职责：
//   - 维护高亮规则列表
//   - 规则去重（条件相同视为同一条）
//   - 根据 DataManager 计算满足条件的 X 轴区间
//   - 深拷贝支持（用于图窗复制）
//   - 通过 std::function 回调通知 UI 层
//
// 每个 PlotPageInfo 持有一个 HighlightManager 实例
// ============================================================
class VIEWER_API HighlightManager
{
public:
    HighlightManager() = default;
    ~HighlightManager() = default;

    // ---- 禁止拷贝，允许移动 ----
    HighlightManager(const HighlightManager&) = delete;
    HighlightManager& operator=(const HighlightManager&) = delete;
    HighlightManager(HighlightManager&&) noexcept = default;
    HighlightManager& operator=(HighlightManager&&) noexcept = default;

    // ============================================================
    // 规则管理
    // ============================================================

    // 添加规则，条件部分相同则视为重复，拒绝添加
    // 返回 true 表示添加成功
    bool addRule(const HighlightRule& rule);

    // 按索引移除规则，返回是否成功
    bool removeRule(size_t index);

    // 获取规则（只读）
    const HighlightRule& rule(size_t index) const;

    // 规则数量
    size_t ruleCount() const noexcept { return m_rules.size(); }

    // 获取全部规则列表（只读）
    const std::vector<HighlightRule>& rules() const noexcept { return m_rules; }

    // 清空所有规则
    void clearAll();

    // ============================================================
    // 区间计算
    // ============================================================

    // 根据 DataManager 计算满足所有规则的 X 轴高亮区间
    // 算法：
    //   1. 对每条规则，逐行检查数据列是否满足条件
    //   2. 将连续满足条件的行索引合并为区间
    //   3. 将行索引区间映射到 X 轴坐标
    //   4. 返回所有高亮区间
    // 若所选数据列不存在或行数为 0，返回空列表
    std::vector<HighlightInterval> computeIntervals(DataManager& dm) const;

    // ============================================================
    // 深拷贝支持（用于图窗复制）
    // ============================================================

    // 深拷贝全部规则
    std::vector<HighlightRule> copyAllRules() const;

    // 写入全部规则（用移动语义注入，用于复制图窗）
    void insertAllRules(std::vector<HighlightRule>&& rules);

    // ============================================================
    // 回调（UI 层绑定）
    // ============================================================

    // 规则变更后触发（添加/移除/清空）
    std::function<void()> onRulesChanged;

private:
    std::vector<HighlightRule> m_rules;
};

} // namespace viewer