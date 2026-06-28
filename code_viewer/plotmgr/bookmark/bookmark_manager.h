#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/plotmgr/highlight/highlight_manager.h"
#include <string>
#include <vector>
#include <qjsonobject.h>

namespace viewer
{

// ============================================================
// GraphStyleSnapshot: 单条曲线的样式快照
// ============================================================
struct GraphStyleSnapshot
{
    std::string dataItemName;       // 数据项名称
    int         penStyle    = 1;    // Qt::PenStyle
    int         penWidth    = 1;
    std::string penColor    = "#0072bd";
    int         scatterShape = 0;   // QCPScatterStyle::ScatterShape
    int         scatterSize  = 6;
    std::string scatterColor = "#000000";
    std::string expressionText;     // 表达式文本
    bool        isEdited     = false;
};

// ============================================================
// BookmarkEntry: 单个收藏项（保存一个图窗的完整状态）
// ============================================================
struct BookmarkEntry
{
    std::string                    name;            // 收藏项名称（全局唯一）
    size_t                         xAxisColumn = static_cast<size_t>(-1);
    bool                           legendVisible = false;
    std::vector<std::string>       dataItems;       // Y 列名集合
    std::vector<GraphStyleSnapshot> graphs;          // 每列样式快照
    std::vector<HighlightRule>     highlights;      // 高亮规则
};

// ============================================================
// BookmarkFolder: 文件夹节点（可嵌套）
// ============================================================
struct BookmarkFolder
{
    std::string                  name;          // 文件夹名（同级内唯一）
    std::vector<BookmarkFolder>  subFolders;    // 子文件夹
    std::vector<BookmarkEntry>   entries;       // 本文件夹内的收藏项
};

// ============================================================
// BookmarkMgr: 收藏夹管理器（纯 C++，无 Qt 依赖）
//
// 职责：
//   - 维护树形文件夹结构
//   - 全局名称唯一性检查
//   - JSON 序列化/反序列化
//   - 提供 CRUD 接口供 UI 层调用
//
// 路径约定：
//   - 使用 "/" 分隔的字符串表示从 root 到目标文件夹的路径
//   - 空字符串 "" 表示 root 层
//   - 例如 "folder1/subfolder" 表示 root → folder1 → subfolder
// ============================================================
class VIEWER_API BookmarkMgr
{
public:
    BookmarkMgr() = default;
    ~BookmarkMgr() = default;

    // ============================================================
    // 文件 I/O
    // ============================================================

    // 从 JSON 文件加载书签树
    void loadFromFile(const std::string& path);

    // 保存书签树到 JSON 文件
    void saveToFile(const std::string& path);

    // ============================================================
    // 全局名称唯一性
    // ============================================================

    // 检查名称在整个树中是否已存在（文件夹和收藏项共享命名空间）
    bool isNameUnique(const std::string& name) const;

    // 判断名称是否已存在（与 isNameUnique 相反）
    bool exists(const std::string& name) const { return !isNameUnique(name); }

    // ============================================================
    // 文件夹 CRUD
    // ============================================================

    // 在指定路径下新建文件夹，返回是否成功
    bool addFolder(const std::string& parentPath, const std::string& name);

    // 删除指定路径的文件夹（级联删除内部所有内容），返回是否成功
    bool removeFolder(const std::string& path);

    // 重命名文件夹，返回是否成功
    bool renameFolder(const std::string& path, const std::string& newName);

    // 移动文件夹到目标父路径下，返回是否成功
    bool moveFolder(const std::string& fromPath, const std::string& toParentPath);

    // ============================================================
    // 收藏项 CRUD
    // ============================================================

    // 在指定文件夹下添加收藏项，返回是否成功
    bool addBookmark(const std::string& folderPath, const BookmarkEntry& entry);

    // 删除指定文件夹下的收藏项，返回是否成功
    bool removeBookmark(const std::string& folderPath, const std::string& name);

    // 重命名收藏项，返回是否成功
    bool renameBookmark(const std::string& folderPath, const std::string& oldName, const std::string& newName);

    // 移动收藏项到目标文件夹，返回是否成功
    bool moveBookmark(const std::string& fromPath, const std::string& toPath, const std::string& name);

    // ============================================================
    // 查询
    // ============================================================

    // 查找指定路径文件夹下的收藏项，未找到返回 nullptr
    const BookmarkEntry* find(const std::string& folderPath, const std::string& name) const;

    // 获取 root 文件夹的只读引用
    const BookmarkFolder& root() const noexcept { return m_root; }

    // 获取 root 文件夹的可写引用
    BookmarkFolder& root() noexcept { return m_root; }

    // 检查指定路径的文件夹是否存在
    bool hasFolder(const std::string& path) const { return findFolder(path) != nullptr; }

    // 清空所有内容
    void clear() { m_root = BookmarkFolder(); }

private:
    // ---- 内部辅助：递归查找文件夹 ----
    BookmarkFolder*       findFolder(const std::string& path);
    const BookmarkFolder* findFolder(const std::string& path) const;

    // ---- 内部辅助：收集所有名称（用于唯一性检查）----
    void collectAllNames(const BookmarkFolder& folder, std::vector<std::string>& out) const;

    // ---- JSON 序列化辅助 ----
    void folderToJson(const BookmarkFolder& folder, QJsonObject& out) const;
    void jsonToFolder(const QJsonObject& obj, BookmarkFolder& out);
    void entryToJson(const BookmarkEntry& entry, QJsonObject& out) const;
    void jsonToEntry(const QJsonObject& obj, BookmarkEntry& out);
    void graphToJson(const GraphStyleSnapshot& gs, QJsonObject& out) const;
    void jsonToGraph(const QJsonObject& obj, GraphStyleSnapshot& out);
    void highlightToJson(const HighlightRule& rule, QJsonObject& out) const;
    void jsonToHighlight(const QJsonObject& obj, HighlightRule& out);

    BookmarkFolder m_root;    // 虚拟 root，其 subFolders 和 entries 为顶层内容
};

} // namespace viewer
