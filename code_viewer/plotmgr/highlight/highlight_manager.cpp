#include "code_viewer/plotmgr/highlight/highlight_manager.h"
#include "code_viewer/datamgr/data_manager.h"
#include <algorithm>
#include <cmath>

namespace viewer
{

// ============================================================
// 规则管理
// ============================================================

bool HighlightManager::addRule(const HighlightRule& rule)
{
    // 条件相同视为重复
    for (const auto& r : m_rules)
    {
        if (r == rule)
            return false;
    }

    m_rules.push_back(rule);

    if (onRulesChanged)
        onRulesChanged();

    return true;
}

bool HighlightManager::removeRule(size_t index)
{
    if (index >= m_rules.size())
        return false;

    m_rules.erase(m_rules.begin() + index);

    if (onRulesChanged)
        onRulesChanged();

    return true;
}

const HighlightRule& HighlightManager::rule(size_t index) const
{
    return m_rules[index];
}

void HighlightManager::clearAll()
{
    if (m_rules.empty())
        return;

    m_rules.clear();

    if (onRulesChanged)
        onRulesChanged();
}

// ============================================================
// 区间计算
// ============================================================

std::vector<HighlightInterval> HighlightManager::computeIntervals(DataManager& dm) const
{
    std::vector<HighlightInterval> result;

    if (m_rules.empty())
        return result;

    size_t rowCount = dm.GetRowCount();
    if (rowCount == 0)
        return result;

    size_t xIdx = dm.GetXAxisColumn();
    if (xIdx == static_cast<size_t>(-1))
        return result;

    const Column* xCol = dm.GetColumn(xIdx);
    if (!xCol || xCol->size() == 0)
        return result;

    // 逐条规则计算区间
    for (const auto& rule : m_rules)
    {
        size_t colIdx = dm.GetColumnIndex(rule.dataColumn);
        if (colIdx == static_cast<size_t>(-1))
            continue;

        const Column* dataCol = dm.GetColumn(colIdx);
        if (!dataCol || dataCol->size() == 0)
            continue;

        size_t n = std::min(rowCount, dataCol->size());

        // 逐行检查条件
        auto checkCondition = [&rule](double val) -> bool
        {
            switch (rule.condition)
            {
            case HighlightCondition::Greater:
                return val > rule.value1;
            case HighlightCondition::Less:
                return val < rule.value1;
            case HighlightCondition::Equal:
                return std::abs(val - rule.value1) < 1e-12;
            case HighlightCondition::NotEqual:
                return std::abs(val - rule.value1) >= 1e-12;
            case HighlightCondition::Between:
                return val >= rule.value1 && val <= rule.value2;
            default:
                return false;
            }
        };

        // 扫描连续满足条件的区间
        size_t segStart = static_cast<size_t>(-1);
        for (size_t i = 0; i < n; ++i)
        {
            double val = dataCol->getDouble(i);
            bool satisfied = checkCondition(val);

            if (satisfied && segStart == static_cast<size_t>(-1))
            {
                segStart = i;
            }
            else if (!satisfied && segStart != static_cast<size_t>(-1))
            {
                // 结束一个区间
                HighlightInterval interval;
                interval.xStart = xCol->getDouble(segStart);
                interval.xEnd = xCol->getDouble(i - 1);
                interval.color = rule.color;
                interval.alpha = rule.alpha;
                interval.label = rule.label;
                result.push_back(std::move(interval));

                segStart = static_cast<size_t>(-1);
            }
        }

        // 处理最后一个区间
        if (segStart != static_cast<size_t>(-1))
        {
            HighlightInterval interval;
            interval.xStart = xCol->getDouble(segStart);
            interval.xEnd = xCol->getDouble(n - 1);
            interval.color = rule.color;
            interval.alpha = rule.alpha;
            interval.label = rule.label;
            result.push_back(std::move(interval));
        }
    }

    return result;
}

// ============================================================
// 深拷贝支持
// ============================================================

std::vector<HighlightRule> HighlightManager::copyAllRules() const
{
    return m_rules;
}

void HighlightManager::insertAllRules(std::vector<HighlightRule>&& rules)
{
    m_rules = std::move(rules);

    if (onRulesChanged)
        onRulesChanged();
}

} // namespace viewer