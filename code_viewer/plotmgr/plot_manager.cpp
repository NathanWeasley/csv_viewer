#include "code_viewer/plotmgr/plot_manager.h"
#include "code_viewer/base/trace_logger.h"
#include <algorithm>

namespace viewer
{

// ============================================================
// 页面管理
// ============================================================

int PlotManager::addPage(const std::string& title)
{
    int index = static_cast<int>(m_pages.size());
    trace::write(trace::Category::Operation,
                 QString("plot manager add page enter index=%1 requestedTitle=\"%2\" pagesBefore=%3")
                     .arg(index).arg(QString::fromStdString(title)).arg(m_pages.size()));

    PlotPageInfo info;
    info.title = title.empty() ? generatePageTitle() : title;
    info.useIndexXAxis = m_newPageUseIndexXAxis;
    info.xAxisColumn = m_newPageXAxisColumn;
    m_pages.push_back(std::move(info));

    // 如果之前没有激活页面，将新页面设为激活
    if (m_activeIndex < 0)
        m_activeIndex = index;

    // 通知 UI 层
    if (onPageAdded)
        onPageAdded(index);

    trace::write(trace::Category::Operation,
                 QString("plot manager add page leave index=%1 title=\"%2\" pagesAfter=%3")
                     .arg(index).arg(QString::fromStdString(m_pages[index].title)).arg(m_pages.size()));
    return index;
}

bool PlotManager::removePage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size()))
        return false;

    trace::write(trace::Category::Operation,
                 QString("plot manager remove page enter index=%1 pagesBefore=%2 activePage=%3")
                     .arg(index).arg(m_pages.size()).arg(m_activeIndex));

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

    trace::write(trace::Category::Operation,
                 QString("plot manager remove page leave index=%1 pagesAfter=%2 activePage=%3")
                     .arg(index).arg(remaining).arg(m_activeIndex));
    return true;
}

void PlotManager::setActivePage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_pages.size()))
        return;
    if (m_activeIndex == index)
        return;

    m_activeIndex = index;
    trace::write(trace::Category::Operation,
                 QString("plot manager active page changed page=%1 pages=%2")
                     .arg(index).arg(m_pages.size()));

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

    // FFT 图窗禁止添加数据项
    if (m_pages[pageIndex].isFFT)
        return false;

    auto& items = m_pages[pageIndex].dataItems;
    if (items.count(yColName) > 0)
        return false;  // 已存在，跳过

    items.insert(yColName);

    trace::write(trace::Category::Operation,
                 QString("plot manager add data enter page=%1 name=\"%2\" itemsAfterInsert=%3")
                     .arg(pageIndex).arg(QString::fromStdString(yColName)).arg(items.size()));

    if (onDataItemAdded)
        onDataItemAdded(pageIndex, yColName);

    trace::write(trace::Category::Operation,
                 QString("plot manager add data leave page=%1 name=\"%2\"")
                     .arg(pageIndex).arg(QString::fromStdString(yColName)));
    return true;
}

bool PlotManager::removeDataItem(int pageIndex, const std::string& yColName)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;

    // FFT 图窗禁止移除数据项
    if (m_pages[pageIndex].isFFT)
        return false;

    auto& items = m_pages[pageIndex].dataItems;
    size_t removed = items.erase(yColName);
    if (removed == 0)
        return false;

    trace::write(trace::Category::Operation,
                 QString("plot manager remove data enter page=%1 name=\"%2\" remaining=%3")
                     .arg(pageIndex).arg(QString::fromStdString(yColName)).arg(items.size()));

    if (onDataItemRemoved)
        onDataItemRemoved(pageIndex, yColName);

    trace::write(trace::Category::Operation,
                 QString("plot manager remove data leave page=%1 name=\"%2\"")
                     .arg(pageIndex).arg(QString::fromStdString(yColName)));
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
    trace::write(trace::Category::Operation,
                 QString("plot manager clear enter pages=%1 activePage=%2")
                     .arg(m_pages.size()).arg(m_activeIndex));
    m_pages.clear();
    m_activeIndex = -1;
    m_nextPageNumber = 1;

    if (onCleared)
        onCleared();
    trace::write(trace::Category::Operation, "plot manager clear leave");
}

// ============================================================
// X 轴管理
// ============================================================

size_t PlotManager::xAxisColumn(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return static_cast<size_t>(-1);
    return m_pages[pageIndex].useIndexXAxis
        ? static_cast<size_t>(-1)
        : m_pages[pageIndex].xAxisColumn;
}

void PlotManager::setXAxisColumn(int pageIndex, size_t colIdx)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;

    if (colIdx == static_cast<size_t>(-1))
    {
        setUseIndexXAxis(pageIndex, true);
        return;
    }

    setXAxisState(pageIndex, false, colIdx);
}

bool PlotManager::usesIndexXAxis(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return true;
    return m_pages[pageIndex].useIndexXAxis;
}

size_t PlotManager::selectedXAxisColumn(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return static_cast<size_t>(-1);
    return m_pages[pageIndex].xAxisColumn;
}

void PlotManager::setUseIndexXAxis(int pageIndex, bool useIndex)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;
    setXAxisState(pageIndex, useIndex, m_pages[pageIndex].xAxisColumn);
}

void PlotManager::setXAxisState(int pageIndex, bool useIndex, size_t colIdx)
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return;

    auto& page = m_pages[pageIndex];
    if (page.useIndexXAxis == useIndex && page.xAxisColumn == colIdx)
        return;

    page.useIndexXAxis = useIndex;
    page.xAxisColumn = colIdx;
    const size_t activeColumn = useIndex ? static_cast<size_t>(-1) : colIdx;

    trace::write(trace::Category::XAxis,
                 QString("plot manager set X-axis enter page=%1 useIndex=%2 selectedColumn=%3 activeColumn=%4")
                     .arg(pageIndex).arg(useIndex).arg(colIdx).arg(activeColumn));

    if (onXAxisChanged)
        onXAxisChanged(pageIndex, activeColumn);
    trace::write(trace::Category::XAxis,
                 QString("plot manager set X-axis leave page=%1 useIndex=%2 selectedColumn=%3")
                     .arg(pageIndex).arg(useIndex).arg(colIdx));
}

void PlotManager::setNewPageXAxisDefaults(bool useIndex, size_t colIdx)
{
    m_newPageUseIndexXAxis = useIndex;
    m_newPageXAxisColumn = colIdx;
    trace::write(trace::Category::XAxis,
                 QString("plot manager new-page X-axis defaults useIndex=%1 column=%2")
                     .arg(useIndex).arg(colIdx));
}

size_t PlotManager::activeXAxisColumn() const
{
    return xAxisColumn(m_activeIndex);
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
// FFT 管理
// ============================================================

int PlotManager::addFFTPage(const std::string& title)
{
    std::string pageTitle = title.empty() ? ("FFT " + std::to_string(m_nextPageNumber)) : title;
    int index = addPage(pageTitle);
    m_pages[index].isFFT = true;
    m_nextPageNumber++;
    return index;
}

bool PlotManager::isFFTPage(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size()))
        return false;
    return m_pages[pageIndex].isFFT;
}

void PlotManager::setLayoutMode(PlotLayoutMode mode)
{
    if (m_layoutMode == mode)
        return;

    m_layoutMode = mode;

    trace::write(trace::Category::Layout,
                 QString("plot manager set layout enter mode=%1 pages=%2")
                     .arg(static_cast<int>(mode)).arg(m_pages.size()));

    if (onLayoutModeChanged)
        onLayoutModeChanged(mode);
    trace::write(trace::Category::Layout,
                 QString("plot manager set layout leave mode=%1").arg(static_cast<int>(mode)));
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
