#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/plotmgr/highlight/highlight_manager.h"
#include <string>
#include <vector>
#include <cstddef>

namespace viewer
{

// ============================================================
// GraphStyleSnapshot: 单个 graph 的样式快照
// ============================================================
struct GraphStyleSnapshot
{
    std::string dataItemName;
    int  penStyle  = 1;         // Qt::SolidLine
    int  penWidth  = 1;
    std::string penColor;       // QColor::name() hex format, e.g. "#ff5733"
    int  scatterShape = 0;      // QCPScatterStyle::ssNone
    int  scatterSize  = 0;
    std::string scatterColor;
    std::string expressionText; // 空 = 未编辑
    bool isEdited = false;
};

// ============================================================
// BookmarkEntry: 一条收藏夹记录
// ============================================================
struct BookmarkEntry
{
    std::string name;                            // 收藏名称（唯一）
    size_t      xAxisColumn = static_cast<size_t>(-1);  // X 轴列索引
    std::vector<std::string>      dataItems;      // Y 列名列表
    bool         legendVisible = false;
    std::vector<GraphStyleSnapshot> graphs;
    std::vector<HighlightRule>       highlights;   // 复用 HighlightRule 结构
};

// ============================================================
// BookmarkMgr: 收藏夹管理器（纯 C++，无 Qt 依赖）
// ============================================================
class VIEWER_API BookmarkMgr
{
public:
    BookmarkMgr() = default;

    // ---- 序列化 ----
    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    // ---- 增删查 ----
    bool add(const BookmarkEntry& entry);        // 返回 false = 重名
    bool remove(const std::string& name);
    bool exists(const std::string& name) const;
    const BookmarkEntry* find(const std::string& name) const;

    // ---- 遍历 ----
    const std::vector<BookmarkEntry>& entries() const noexcept { return m_entries; }
    size_t count() const noexcept { return m_entries.size(); }

private:
    std::vector<BookmarkEntry> m_entries;
};

} // namespace viewer