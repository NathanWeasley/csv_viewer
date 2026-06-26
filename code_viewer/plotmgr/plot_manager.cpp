#include "code_viewer/plotmgr/plot_manager.h"
#include <algorithm>

namespace viewer
{

// ============================================================
// 页面管理
// ============================================================

int PlotManager::addPage(const std::string& title)
{
    int index = static_cast<int>(m_pages.size());

    PlotPageInfo info;
    info.title = title.empty() ? generatePageTitle() : title;
    m_pages.push_back(std::move(info));

    // 如果之前没有激活页面，将新页面设为激活
    if (m_activeIndex < 0)
        m_activeIndex = index;

    // 通知 UI 层
    if (onPageAdded)
        onPageAdded(index);

    return index;
}

bool PlotManager::removePage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size()))
        return false;

    // 通知 UI 层即将移除
    if (onPageAboutToRemove)
        onPageAboutToRemove(index);

    m_pages.erase(m_pages.begin() + index);

    // 调整激活索引
    int remaining = static_cast<int>(m_pages.size());
    if (remaining == 0)
    {
        m_activeIndex = -1;
    }
    else if (m_activeIndex >= remaining)
    {
        m_activeIndex = remaining - 1;
    }
    else if (m_activeIndex > index)
    {
        // 删除在激活页之前，索引偏移无需处理（激活索引本身已正确）
        // 但注意：上面 erase 后 index 之后的元素前移，m_activeIndex 需调整
        --m_activeIndex;
    }
    // 如果 m_activeIndex == index 且 remaining > 0，激活页变为被删页的下一个（或前一个）
    else if (m_activeIndex == index && remaining > 0)
    {
        // 保留当前索引位置即可（erase 后新 m_pages[index] 是原来 index+1 的元素）
    }

    // 通知 UI 层移除完成
    if (onPageRemoved)
        onPageRemoved(m_activeIndex, remaining);

    // 最后一页被移除时重置计数器
    if (remaining == 0)
        m_nextPageNumber = 1;

    return true;
}

void PlotManager::setActivePage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size()))
        return;
    if (m_activeIndex == index)
        return;

    m_activeIndex = index;

    if (onActivePageChanged)
        onActivePageChanged(index);
}

// ============================================================
// 数据项管理
// ============================================================

bool PlotManager::addDataItem(int pageIndex, const std::string& yColName)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;

    auto& items = m_pages[pageIndex].dataItems;
    if (items.count(yColName) > 0)
        return false;  // 已存在，跳过

    items.insert(yColName);

    if (onDataItemAdded)
        onDataItemAdded(pageIndex, yColName);

    return true;
}

bool PlotManager::removeDataItem(int pageIndex, const std::string& yColName)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;

    auto& items = m_pages[pageIndex].dataItems;
    size_t removed = items.erase(yColName);
    if (removed == 0)
        return false;

    if (onDataItemRemoved)
        onDataItemRemoved(pageIndex, yColName);

    return true;
}

bool PlotManager::hasDataItem(int pageIndex, const std::string& yColName) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;

    return m_pages[pageIndex].dataItems.count(yColName) > 0;
}

bool PlotManager::addDataToActivePage(const std::string& yColName)
{
    // 没有激活页面时自动创建
    if (m_activeIndex < 0)
    {
        int idx = addPage();
        m_activeIndex = idx;
    }

    return addDataItem(m_activeIndex, yColName);
}

void PlotManager::clearDataItems(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;

    m_pages[pageIndex].dataItems.clear();
}

void PlotManager::clearAll()
{
    m_pages.clear();
    m_activeIndex = -1;
    m_nextPageNumber = 1;

    if (onCleared)
        onCleared();
}

// ============================================================
// 查询
// ============================================================

const PlotPageInfo& PlotManager::pageInfo(int index) const
{
    return m_pages[index];
}

PlotPageInfo& PlotManager::pageInfo(int index)
{
    return m_pages[index];
}

// ============================================================
// 框选缩放管理
// ============================================================

void PlotManager::setRectZoomActive(int pageIndex, bool active)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;
    if (m_pages[pageIndex].rectZoomActive == active)
        return;

    m_pages[pageIndex].rectZoomActive = active;

    if (onRectZoomStateChanged)
        onRectZoomStateChanged(pageIndex, active);
}

bool PlotManager::isRectZoomActive(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;
    return m_pages[pageIndex].rectZoomActive;
}

// ============================================================
// 图例管理
// ============================================================

void PlotManager::setLegendVisible(int pageIndex, bool visible)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;
    if (m_pages[pageIndex].legendVisible == visible)
        return;

    m_pages[pageIndex].legendVisible = visible;

    if (onLegendVisibilityChanged)
        onLegendVisibilityChanged(pageIndex, visible);
}

bool PlotManager::isLegendVisible(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;
    return m_pages[pageIndex].legendVisible;
}

// ============================================================
// 选中数据项管理
// ============================================================

void PlotManager::setSelectedDataItem(int pageIndex, const std::string& yColName)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;
    if (m_pages[pageIndex].selectedDataItem == yColName)
        return;

    m_pages[pageIndex].selectedDataItem = yColName;

    if (onSelectedDataItemChanged)
        onSelectedDataItemChanged(pageIndex, yColName);
}

const std::string& PlotManager::selectedDataItem(int pageIndex) const
{
    static const std::string empty;
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return empty;
    return m_pages[pageIndex].selectedDataItem;
}

// ============================================================
// 私有
// ============================================================

std::string PlotManager::generatePageTitle()
{
    int num = m_nextPageNumber++;
    return "Plot " + std::to_string(num);
}

} // namespace viewer