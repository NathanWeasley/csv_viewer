#include "code_viewer/plotmgr/cursor/cursor_manager.h"

#include <algorithm>

namespace viewer
{

// ============================================================
// 预选管理
// ============================================================

void CursorManager::setPreSelection(int pageIndex, const std::string& dataItemName, size_t dataIndex)
{
    // 与当前预选相同则忽略
    if (m_hasPreSel
        && m_preSelPage == pageIndex
        && m_preSelItem == dataItemName
        && m_preSelIndex == dataIndex)
    {
        return;
    }

    m_hasPreSel   = true;
    m_preSelPage  = pageIndex;
    m_preSelItem  = dataItemName;
    m_preSelIndex = dataIndex;

    if (onPreSelectionSet)
        onPreSelectionSet(pageIndex, dataItemName, dataIndex);
}

void CursorManager::clearPreSelection()
{
    if (!m_hasPreSel)
        return;

    m_hasPreSel   = false;
    m_preSelPage  = -1;
    m_preSelItem.clear();
    m_preSelIndex = 0;

    if (onPreSelectionCleared)
        onPreSelectionCleared();
}

// ============================================================
// 游标管理
// ============================================================

int CursorManager::addCursor(int pageIndex, const std::string& dataItemName, size_t dataIndex)
{
    setActiveAxisCursor(-1);

    // 取消旧的激活
    int oldActive = m_activeCursorIdx;
    if (oldActive >= 0 && oldActive < static_cast<int>(m_cursors.size()))
    {
        m_cursors[oldActive].isActive = false;
    }

    CursorInfo info;
    info.pageIndex    = pageIndex;
    info.dataItemName = dataItemName;
    info.dataIndex    = dataIndex;
    info.isActive     = true;

    m_cursors.push_back(info);
    m_activeCursorIdx = static_cast<int>(m_cursors.size()) - 1;

    if (onCursorAdded)
        onCursorAdded(m_activeCursorIdx, pageIndex, dataItemName, dataIndex);

    return m_activeCursorIdx;
}

bool CursorManager::removeCursor(int index)
{
    if (index < 0 || index >= static_cast<int>(m_cursors.size()))
        return false;

    m_cursors.erase(m_cursors.begin() + index);

    // 调整激活索引
    int sz = static_cast<int>(m_cursors.size());
    if (index == m_activeCursorIdx)
    {
        m_activeCursorIdx = -1;
    }
    else if (index < m_activeCursorIdx)
    {
        --m_activeCursorIdx;
    }

    if (onCursorRemoved)
        onCursorRemoved(index);

    return true;
}

bool CursorManager::removeActiveCursor()
{
    if (m_activeCursorIdx < 0 || m_activeCursorIdx >= static_cast<int>(m_cursors.size()))
        return false;

    return removeCursor(m_activeCursorIdx);
}

bool CursorManager::moveCursorToIndex(int cursorIdx, size_t newDataIndex)
{
    if (cursorIdx < 0 || cursorIdx >= static_cast<int>(m_cursors.size()))
        return false;

    m_cursors[cursorIdx].dataIndex = newDataIndex;

    // 如果移动的是激活游标，触发 onActiveCursorChanged 让 UI 更新 tracer/浮窗
    if (cursorIdx == m_activeCursorIdx)
    {
        if (onActiveCursorChanged)
            onActiveCursorChanged(cursorIdx);
    }

    return true;
}

void CursorManager::setActiveCursor(int index)
{
    if (index >= 0)
        setActiveAxisCursor(-1);

    if (index == m_activeCursorIdx)
        return;

    if (index < -1 || index >= static_cast<int>(m_cursors.size()))
        return;

    // 取消旧激活
    if (m_activeCursorIdx >= 0 && m_activeCursorIdx < static_cast<int>(m_cursors.size()))
    {
        m_cursors[m_activeCursorIdx].isActive = false;
    }

    m_activeCursorIdx = index;

    // 设置新激活
    if (index >= 0)
    {
        m_cursors[index].isActive = true;
    }

    if (onActiveCursorChanged)
        onActiveCursorChanged(index);
}

int CursorManager::addAxisCursor(int pageIndex, AxisCursorType type, double position)
{
    setActiveCursor(-1);
    setActiveAxisCursor(-1);

    AxisCursorInfo info;
    info.id = m_nextAxisCursorId++;
    info.pageIndex = pageIndex;
    info.type = type;
    info.position = position;
    info.dataValuesVisible = (type == AxisCursorType::X);
    info.isActive = true;

    m_axisCursors.push_back(info);
    m_activeAxisCursorId = info.id;
    if (onAxisCursorAdded)
        onAxisCursorAdded(info.id, m_axisCursors.back());
    if (onActiveAxisCursorChanged)
        onActiveAxisCursorChanged(info.id);
    return info.id;
}

bool CursorManager::removeAxisCursor(int id)
{
    auto it = std::find_if(m_axisCursors.begin(), m_axisCursors.end(),
        [id](const AxisCursorInfo& cursor) { return cursor.id == id; });
    if (it == m_axisCursors.end())
        return false;

    const bool wasActive = (m_activeAxisCursorId == id);
    m_axisCursors.erase(it);
    if (wasActive)
        m_activeAxisCursorId = -1;
    if (onAxisCursorRemoved)
        onAxisCursorRemoved(id);
    if (wasActive && onActiveAxisCursorChanged)
        onActiveAxisCursorChanged(-1);
    return true;
}

void CursorManager::removeAxisCursors(int pageIndex)
{
    std::vector<int> ids;
    for (const auto& cursor : m_axisCursors)
    {
        if (cursor.pageIndex == pageIndex)
            ids.push_back(cursor.id);
    }
    for (int id : ids)
        removeAxisCursor(id);
}

void CursorManager::removeAxisCursors(int pageIndex, AxisCursorType type)
{
    std::vector<int> ids;
    for (const auto& cursor : m_axisCursors)
    {
        if (cursor.pageIndex == pageIndex && cursor.type == type)
            ids.push_back(cursor.id);
    }
    for (int id : ids)
        removeAxisCursor(id);
}

bool CursorManager::setAxisCursorPosition(int id, double position)
{
    auto it = std::find_if(m_axisCursors.begin(), m_axisCursors.end(),
        [id](const AxisCursorInfo& cursor) { return cursor.id == id; });
    if (it == m_axisCursors.end())
        return false;

    it->position = position;
    if (onAxisCursorChanged)
        onAxisCursorChanged(id, *it);
    return true;
}

bool CursorManager::setAxisCursorDataValuesVisible(int id, bool visible)
{
    auto it = std::find_if(m_axisCursors.begin(), m_axisCursors.end(),
        [id](const AxisCursorInfo& cursor) { return cursor.id == id; });
    if (it == m_axisCursors.end() || it->type != AxisCursorType::X)
        return false;

    it->dataValuesVisible = visible;
    if (onAxisCursorChanged)
        onAxisCursorChanged(id, *it);
    return true;
}

void CursorManager::setActiveAxisCursor(int id)
{
    if (id >= 0)
        setActiveCursor(-1);

    if (id == m_activeAxisCursorId)
        return;

    if (id >= 0 && !axisCursor(id))
        return;

    for (auto& cursor : m_axisCursors)
        cursor.isActive = (cursor.id == id);
    m_activeAxisCursorId = id;
    if (onActiveAxisCursorChanged)
        onActiveAxisCursorChanged(id);
}

const AxisCursorInfo* CursorManager::axisCursor(int id) const noexcept
{
    auto it = std::find_if(m_axisCursors.begin(), m_axisCursors.end(),
        [id](const AxisCursorInfo& cursor) { return cursor.id == id; });
    return it == m_axisCursors.end() ? nullptr : &*it;
}

void CursorManager::shiftPageIndicesAfterRemoval(int removedPageIndex)
{
    for (auto& cursor : m_cursors)
    {
        if (cursor.pageIndex > removedPageIndex)
            --cursor.pageIndex;
    }

    if (m_preSelPage > removedPageIndex)
        --m_preSelPage;
    else if (m_preSelPage == removedPageIndex)
        clearPreSelection();

    for (auto& cursor : m_axisCursors)
    {
        if (cursor.pageIndex > removedPageIndex)
            --cursor.pageIndex;
    }
}
void CursorManager::clearAll()
{
    // 先逐个通知移除
    while (!m_cursors.empty())
    {
        int lastIdx = static_cast<int>(m_cursors.size()) - 1;
        m_cursors.pop_back();
        if (onCursorRemoved)
            onCursorRemoved(lastIdx);
    }

    m_activeCursorIdx = -1;
    clearPreSelection();

    while (!m_axisCursors.empty())
    {
        const int id = m_axisCursors.back().id;
        m_axisCursors.pop_back();
        if (onAxisCursorRemoved)
            onAxisCursorRemoved(id);
    }
    if (m_activeAxisCursorId >= 0 && onActiveAxisCursorChanged)
        onActiveAxisCursorChanged(-1);
    m_activeAxisCursorId = -1;
}

} // namespace viewer
