#include "UI.h"

#include "LogTimeMapper.h"
#include "RbtLogViewer.h"

#include <QMessageBox>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool nearestColumnIndex(const viewer::Column* column, double value, size_t& index)
{
    if (!column || column->empty() || !std::isfinite(value))
        return false;
    double bestDistance = std::numeric_limits<double>::infinity();
    size_t bestIndex = 0;
    for (size_t row = 0; row < column->size(); ++row)
    {
        const double current = (*column)[row];
        if (!std::isfinite(current))
            continue;
        const double distance = std::abs(current - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = row;
        }
    }
    if (!std::isfinite(bestDistance))
        return false;
    index = bestIndex;
    return true;
}
}

void UI::showLogJumpFailed()
{
    QMessageBox::warning(
        this, QString::fromUtf8(u8"跳转失败"), QString::fromUtf8(u8"跳转失败"));
}

bool UI::isLogSyncXAxisSupported(int pageIndex) const
{
    const auto& plots = m_viewer.GetPlotManager();
    if (pageIndex < 0 || pageIndex >= plots.pageCount())
        return false;
    return plots.usesIndexXAxis(pageIndex)
        || plots.selectedXAxisColumn(pageIndex)
            == m_viewer.GetDataManager().GetXAxisColumn();
}

void UI::jumpFromPlotToRbt(int pageIndex, QCustomPlot* plot, const QPoint& pos)
{
    auto& data = m_viewer.GetDataManager();
    const auto& plots = m_viewer.GetPlotManager();
    if (!plot || !m_logTimeMapper || !m_logTimeMapper->isIndexed()
        || data.GetRowCount() == 0 || pageIndex < 0 || pageIndex >= plots.pageCount())
    {
        showLogJumpFailed();
        return;
    }
    if (!m_logTimeMapper->isAligned() && !m_logTimeMapper->align(data))
    {
        showLogJumpFailed();
        return;
    }

    size_t dataIndex = 0;
    const double x = plot->xAxis->pixelToCoord(pos.x());
    if (plots.usesIndexXAxis(pageIndex))
    {
        if (!std::isfinite(x))
        {
            showLogJumpFailed();
            return;
        }
        const qint64 rounded = std::llround(x);
        dataIndex = static_cast<size_t>(std::clamp<qint64>(
            rounded, 0, static_cast<qint64>(data.GetRowCount() - 1)));
    }
    else
    {
        const size_t xColumnIndex = plots.selectedXAxisColumn(pageIndex);
        if (!nearestColumnIndex(data.GetColumn(xColumnIndex), x, dataIndex))
        {
            showLogJumpFailed();
            return;
        }
    }

    LogTimeMapper::RbtLocation location;
    if (!m_logTimeMapper->rbtLocationForDataIndex(data, dataIndex, location))
    {
        showLogJumpFailed();
        return;
    }

    ensureRbtLogViewer();
    m_rbtLogViewer->openFiles(m_logTimeMapper->rbtFiles());
    m_rbtLogViewer->openFileAtLine(location.filePath, location.line);
}

void UI::markRbtLineOnPlots(
    const QString& filePath, qsizetype line, bool allPlots)
{
    auto& data = m_viewer.GetDataManager();
    auto& plots = m_viewer.GetPlotManager();
    int activePage = activePlotPage();
    if (activePage < 0)
        activePage = plots.activePageIndex();
    if (!m_logTimeMapper || !m_logTimeMapper->isIndexed()
        || plots.pageCount() == 0 || activePage < 0
        || !isLogSyncXAxisSupported(activePage))
    {
        showLogJumpFailed();
        return;
    }
    if (!m_logTimeMapper->isAligned() && !m_logTimeMapper->align(data))
    {
        showLogJumpFailed();
        return;
    }

    QList<int> targets;
    if (allPlots)
    {
        for (int page = 0; page < plots.pageCount(); ++page)
            targets.push_back(page);
    }
    else
    {
        targets.push_back(activePage);
    }

    size_t dataIndex = 0;
    if (!m_logTimeMapper->dataIndexForRbtLocation(
            data, filePath, line, dataIndex))
    {
        showLogJumpFailed();
        return;
    }

    QList<double> positions;
    positions.reserve(targets.size());
    for (int page : targets)
    {
        double position = static_cast<double>(dataIndex);
        if (!plots.usesIndexXAxis(page))
        {
            const viewer::Column* xColumn = data.GetColumn(
                plots.selectedXAxisColumn(page));
            if (!xColumn || dataIndex >= xColumn->size())
            {
                showLogJumpFailed();
                return;
            }
            position = (*xColumn)[dataIndex];
        }
        if (!std::isfinite(position))
        {
            showLogJumpFailed();
            return;
        }
        positions.push_back(position);
    }
    for (qsizetype index = 0; index < targets.size(); ++index)
        createAxisCursor(targets[index], viewer::AxisCursorType::X, positions[index]);
}
