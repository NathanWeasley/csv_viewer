#include "UI.h"

#include <qtreewidget.h>
#include <qlabel.h>
#include <qsettings.h>
#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qsvgrenderer.h>
#include <qpainter.h>
#include <qstylehints.h>
#include <qregularexpression.h>
#include <qfiledialog.h>
#include <qmessagebox.h>
#include <qclipboard.h>
#include <qmenu.h>
#include <qtabbar.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qinputdialog.h>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

bool isSystemInDark()
{
    Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();

    return (scheme == Qt::ColorScheme::Dark);
}

// ============================================================
// findNearestDataPoint: 在 plot 的所有 graph 中查找最近的数据点
// ============================================================

QPair<viewer::QCPColumnGraph*, size_t> UI::findNearestDataPoint(
    QCustomPlot* plot, const QPoint& mousePos, double pixelThreshold) const
{
    viewer::QCPColumnGraph* bestGraph = nullptr;
    size_t bestIdx = 0;
    double bestDistSq = pixelThreshold * pixelThreshold;

    for (int i = 0; i < plot->plottableCount(); ++i)
    {
        auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
        if (!graph)
            continue;

        int n = graph->dataCount();
        if (n == 0)
            continue;

        // 将鼠标像素坐标转为 key 坐标，二分查找最近的 index
        double mouseKey = plot->xAxis->pixelToCoord(mousePos.x());
        double mouseValue = plot->yAxis->pixelToCoord(mousePos.y());

        // 二分查找第一个 >= mouseKey 的索引
        int lo = 0, hi = n - 1;
        while (lo < hi)
        {
            int mid = (lo + hi) / 2;
            if (graph->dataMainKey(mid) < mouseKey)
                lo = mid + 1;
            else
                hi = mid;
        }
        int closestIdx = lo;

        // 检查左右邻居（±50范围内，限制搜索）
        int searchRange = 50;
        int start = std::max(0, closestIdx - searchRange);
        int end   = std::min(n - 1, closestIdx + searchRange);

        for (int j = start; j <= end; ++j)
        {
            QPointF pixelPos = graph->dataPixelPosition(j);
            double dx = pixelPos.x() - mousePos.x();
            double dy = pixelPos.y() - mousePos.y();
            double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestGraph = graph;
                bestIdx = static_cast<size_t>(j);
            }
        }
    }

    return { bestGraph, bestIdx };
}

// ============================================================
// isDataTooDense: 检查当前图窗数据密度
// ============================================================

bool UI::isDataTooDense(QCustomPlot* plot) const
{
    if (!plot)
        return true;

    if (plot->plottableCount() == 0)
        return true;

    // 当前 X 轴可见范围（数据坐标）
    const QCPRange& keyRange = plot->xAxis->range();

    int totalVisiblePoints = 0;
    for (int i = 0; i < plot->plottableCount(); ++i)
    {
        auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
        if (!graph || graph->dataCount() == 0)
            continue;

        int begin = graph->findBegin(keyRange.lower, true);
        int end   = graph->findEnd(keyRange.upper, true);
        if (end > begin)
            totalVisiblePoints += (end - begin);
    }

    if (totalVisiblePoints <= 0)
        return false;  // 没有可见点时放行

    int plotWidth = plot->axisRect() ? plot->axisRect()->width() : plot->width();
    if (plotWidth <= 0)
        return false;

    double density = static_cast<double>(totalVisiblePoints) / static_cast<double>(plotWidth);
    return density > 2.0;
}
