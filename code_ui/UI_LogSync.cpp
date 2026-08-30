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

void UI::showLogJumpFailed(const QString& direction, const QString& reason)
{
    const QString detail = reason.trimmed().isEmpty()
        ? QString::fromUtf8(u8"未提供失败原因。") : reason.trimmed();
    logOperationTrace(
        QStringLiteral("log jump failed direction=%1 reason=\"%2\"")
            .arg(direction, detail.simplified()));
    QMessageBox::warning(
        this, QString::fromUtf8(u8"日志跳转失败"), detail);
}

void UI::jumpFromPlotToRbt(int pageIndex, QCustomPlot* plot, const QPoint& pos)
{
    const QString direction = QStringLiteral("plot-to-rbt");
    logOperationTrace(
        QStringLiteral("log jump requested direction=%1 page=%2 pixel=(%3,%4)")
            .arg(direction).arg(pageIndex).arg(pos.x()).arg(pos.y()));
    const auto fail = [this, &direction](const QString& reason)
    {
        showLogJumpFailed(direction, reason);
    };
    auto& data = m_viewer.GetDataManager();
    const auto& plots = m_viewer.GetPlotManager();
    if (!plot)
    {
        fail(QString::fromUtf8(u8"图窗对象已失效。"));
        return;
    }
    if (!m_logTimeMapper)
    {
        fail(QString::fromUtf8(u8"日志时间映射器尚未初始化。"));
        return;
    }
    if (!m_logTimeMapper->isIndexed())
    {
        fail(QString::fromUtf8(u8"尚未建立 RBT 日志时间索引，请先载入包含 RBT 日志的数据。"));
        return;
    }
    if (data.GetRowCount() == 0)
    {
        fail(QString::fromUtf8(u8"Viewer 当前没有可用于跳转的数据行。"));
        return;
    }
    if (pageIndex < 0 || pageIndex >= plots.pageCount())
    {
        fail(QString::fromUtf8(u8"发起跳转的图页编号无效。"));
        return;
    }
    QString error;
    if (!m_logTimeMapper->isAligned() && !m_logTimeMapper->align(data, &error))
    {
        fail(error);
        return;
    }

    size_t dataIndex = 0;
    const double x = plot->xAxis->pixelToCoord(pos.x());
    if (plots.usesIndexXAxis(pageIndex))
    {
        if (!std::isfinite(x))
        {
            fail(QString::fromUtf8(u8"鼠标位置无法换算为有效的图窗 X 轴坐标。"));
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
            fail(QString::fromUtf8(u8"图窗 X 轴列不存在，或该列没有可用的有限数值。"));
            return;
        }
    }

    LogTimeMapper::RbtLocation location;
    if (!m_logTimeMapper->rbtLocationForDataIndex(
            data, dataIndex, location, &error))
    {
        fail(error);
        return;
    }

    ensureRbtLogViewer();
    m_rbtLogViewer->openFiles(m_logTimeMapper->rbtFiles());
    logOperationTrace(
        QStringLiteral("log jump mapped direction=%1 page=%2 dataRow=%3 file=\"%4\" line=%5")
            .arg(direction).arg(pageIndex).arg(dataIndex + 1)
            .arg(location.filePath).arg(location.line + 1));
    if (!m_rbtLogViewer->openFileAtLine(location.filePath, location.line, &error))
    {
        fail(error);
        return;
    }
}

void UI::markRbtLineOnPlots(
    const QString& filePath, qsizetype line, bool allPlots)
{
    const QString direction = QStringLiteral("rbt-to-plot");
    logOperationTrace(
        QStringLiteral("log jump requested direction=%1 file=\"%2\" line=%3 scope=%4")
            .arg(direction, filePath).arg(line + 1)
            .arg(allPlots ? QStringLiteral("all-plots") : QStringLiteral("active-plot")));
    const auto fail = [this, &direction](const QString& reason)
    {
        showLogJumpFailed(direction, reason);
    };
    auto& data = m_viewer.GetDataManager();
    auto& plots = m_viewer.GetPlotManager();
    int activePage = activePlotPage();
    if (activePage < 0)
        activePage = plots.activePageIndex();
    if (!m_logTimeMapper)
    {
        fail(QString::fromUtf8(u8"日志时间映射器尚未初始化。"));
        return;
    }
    if (!m_logTimeMapper->isIndexed())
    {
        fail(QString::fromUtf8(u8"尚未建立 RBT 日志时间索引，请先载入包含 RBT 日志的数据。"));
        return;
    }
    if (plots.pageCount() == 0)
    {
        fail(QString::fromUtf8(u8"当前没有可接收标记的图窗。"));
        return;
    }
    if (activePage < 0 || activePage >= plots.pageCount())
    {
        fail(QString::fromUtf8(u8"当前活动图页编号无效。"));
        return;
    }
    QString error;
    if (!m_logTimeMapper->isAligned() && !m_logTimeMapper->align(data, &error))
    {
        fail(error);
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
            data, filePath, line, dataIndex, &error))
    {
        fail(error);
        return;
    }

    QList<double> positions;
    QList<QCPRange> centeredRanges;
    positions.reserve(targets.size());
    centeredRanges.reserve(targets.size());
    for (int page : targets)
    {
        double position = static_cast<double>(dataIndex);
        if (!plots.usesIndexXAxis(page))
        {
            const viewer::Column* xColumn = data.GetColumn(
                plots.selectedXAxisColumn(page));
            if (!xColumn || dataIndex >= xColumn->size())
            {
                fail(QString::fromUtf8(u8"图页 %1 的 X 轴列不存在或行数不足。")
                         .arg(page + 1));
                return;
            }
            position = (*xColumn)[dataIndex];
        }
        if (!std::isfinite(position))
        {
            fail(QString::fromUtf8(u8"图页 %1 在目标数据行的 X 轴值不是有限数值。")
                     .arg(page + 1));
            return;
        }
        positions.push_back(position);

        QCustomPlot* plot = getPlot(page);
        if (!plot || !plot->xAxis)
        {
            fail(QString::fromUtf8(u8"图页 %1 的绘图对象或 X 轴不可用。")
                     .arg(page + 1));
            return;
        }
        const QCPRange currentRange = plot->xAxis->range();
        const double rangeSize = currentRange.size();
        if (!std::isfinite(currentRange.lower)
            || !std::isfinite(currentRange.upper)
            || !(rangeSize > 0.0) || !std::isfinite(rangeSize))
        {
            fail(QString::fromUtf8(u8"图页 %1 的当前 X 轴范围无效，无法保持缩放并居中。")
                     .arg(page + 1));
            return;
        }
        const double halfRange = rangeSize * 0.5;
        const QCPRange centeredRange(position - halfRange, position + halfRange);
        if (!std::isfinite(centeredRange.lower)
            || !std::isfinite(centeredRange.upper))
        {
            fail(QString::fromUtf8(u8"图页 %1 的目标 X 轴范围超出有效数值范围。")
                     .arg(page + 1));
            return;
        }
        centeredRanges.push_back(centeredRange);
    }

    // 先平移可视范围，避免位于当前视区外的目标光标被钳制到边界。
    // setRange 会触发现有的 X 轴联动，同组的其他图窗将保持相同缩放并同步居中。
    for (qsizetype index = 0; index < targets.size(); ++index)
    {
        if (QCustomPlot* plot = getPlot(targets[index]))
        {
            plot->xAxis->setRange(centeredRanges[index]);
            plot->replot(QCustomPlot::rpQueuedRefresh);
        }
    }
    for (qsizetype index = 0; index < targets.size(); ++index)
        createAxisCursor(targets[index], viewer::AxisCursorType::X, positions[index]);
    QStringList targetNames;
    targetNames.reserve(targets.size());
    for (int page : targets)
        targetNames.push_back(QString::number(page));
    logOperationTrace(
        QStringLiteral("log jump succeeded direction=%1 file=\"%2\" line=%3 dataRow=%4 pages=%5 centered=1 preserveZoom=1")
            .arg(direction, filePath).arg(line + 1).arg(dataIndex + 1)
            .arg(targetNames.join(QLatin1Char(','))));
}
