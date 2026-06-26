#include "code_viewer/plotmgr/cursor/cursor_manager.h"

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
}

} // namespace viewer