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
#include <limits>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

// ============================================================
// CursorManager → Qt 控件绑定
// ============================================================

void UI::bindCursorManagerCallbacks()
{
    auto& cm = m_viewer.GetCursorManager();
    auto& pm = m_viewer.GetPlotManager();

    // ---- 扩展 onPageAdded：在 plot 创建后附加 tracer ----
    auto originalOnPageAdded = pm.onPageAdded;

    pm.onPageAdded = [this, originalOnPageAdded](int index)
    {
        if (originalOnPageAdded)
            originalOnPageAdded(index);

        if (index < 0 || index >= plotPageCount())
            return;

        auto* container = getPlotContainer(index);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        auto* tracer = new QCPItemTracer(plot);
        tracer->setVisible(false);
        tracer->setInterpolating(false);
        // 从 StyleManager 读取临时游标样式
        {
            auto& ts = m_viewer.GetStyleManager().cursorStyle("temporary");
            tracer->setStyle(QCPItemTracer::tsCircle);
            tracer->setSize(ts.size);
            QColor sc = ts.strokeActive.toQColor();
            tracer->setPen(QPen(sc, ts.strokeWidthActive));
            QColor bc = ts.fillActive.toQColor();
            bc.setAlpha(static_cast<int>(255 * ts.opacityActive));
            tracer->setBrush(bc);
        }

        m_plotToPageIndex[plot] = index;
        m_preSelTracers[plot] = tracer;
    };

    // ---- 扩展 onPageAboutToRemove：清理该页面的 tracer 和相关游标 ----
    auto originalOnPageAboutToRemove = pm.onPageAboutToRemove;

    pm.onPageAboutToRemove = [this, originalOnPageAboutToRemove](int index)
    {
        // 获取即将被移除的 plot，清理 m_preSelTracers
        if (index >= 0 && index < plotPageCount())
        {
            auto* container = getPlotContainer(index);
            auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
            if (plot)
            {
                m_plotToPageIndex.remove(plot);
                m_preSelTracers.remove(plot);
            }
        }

        // 移除属于该页面的所有游标（从后往前删除）
        auto& cm = m_viewer.GetCursorManager();
        auto& cursors = cm.cursors();
        // 收集需要删除的索引
        std::vector<int> toRemove;
        for (size_t i = 0; i < cursors.size(); ++i)
        {
            if (cursors[i].pageIndex == index)
                toRemove.push_back(static_cast<int>(i));
        }
        // 从后往前删除（避免索引偏移）
        for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        {
            cm.removeCursor(*it);
        }

        // 清除可能存在的预选
        cm.clearPreSelection();

        // ---- 清理高亮元素映射 ----
        m_highlightRects.remove(index);
        m_highlightLabels.remove(index);

        // 执行原有逻辑（删除 widget）
        if (originalOnPageAboutToRemove)
            originalOnPageAboutToRemove(index);
    };

    // ---- 扩展 onCleared：清理所有 cursor 状态 ----
    auto originalOnCleared = pm.onCleared;
    pm.onCleared = [this, originalOnCleared]()
    {
        auto& cm = m_viewer.GetCursorManager();
        cm.clearAll();
        m_plotToPageIndex.clear();
        m_preSelTracers.clear();
        m_cursorTracers.clear();
        m_cursorLabels.clear();
        m_cursorConnectorLines.clear();
        m_highlightRects.clear();
        m_highlightLabels.clear();

        if (originalOnCleared)
            originalOnCleared();
    };

    // ---- 预选设置 → 显示 tracer + 更新状态栏 ----
    cm.onPreSelectionSet = [this](int pageIndex, const std::string& dataItemName, size_t dataIndex)
    {
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        // 查找 graph
        viewer::QCPColumnGraph* graph = nullptr;
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* p = plot->plottable(i);
            if (p && p->name().toStdString() == dataItemName)
            {
                graph = dynamic_cast<viewer::QCPColumnGraph*>(p);
                break;
            }
        }
        if (!graph)
            return;

        auto* tracer = m_preSelTracers.value(plot, nullptr);
        if (!tracer)
            return;

        double xKey = graph->dataMainKey(static_cast<int>(dataIndex));
        double yVal = graph->dataMainValue(static_cast<int>(dataIndex));
        tracer->position->setCoords(xKey, yVal);
        tracer->setVisible(true);
        plot->replot(); // tracer 显隐/位置变更需要 replot（QCPItemTracer 有此特性）

        // 更新状态栏
        double x = graph->dataMainKey(static_cast<int>(dataIndex));
        double y = graph->dataMainValue(static_cast<int>(dataIndex));
        if (m_cursorStatusLabel)
        {
            // 格式化显示，保留合理位数
            m_cursorStatusLabel->setText(
                QString("X: %1  Y: %2")
                    .arg(x, 0, 'g', 8)
                    .arg(y, 0, 'g', 8));
        }
    };

    // ---- 预选清除 → 隐藏 tracer + 清除状态栏 ----
    cm.onPreSelectionCleared = [this]()
    {
        for (auto* tracer : m_preSelTracers)
        {
            if (tracer && tracer->visible())
            {
                auto* plot = tracer->parentPlot();
                tracer->setVisible(false);
                if (plot)
                    plot->replot();
            }
        }

        if (m_cursorStatusLabel)
            m_cursorStatusLabel->clear();
    };

    // ---- 游标添加 → 创建 QCPItemText 数据标签 + tracer ----
    cm.onCursorAdded = [this](int cursorIdx, int pageIndex,
                                const std::string& dataItemName, size_t dataIndex)
    {
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        // 查找 graph
        viewer::QCPColumnGraph* graph = nullptr;
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* p = plot->plottable(i);
            if (p && p->name().toStdString() == dataItemName)
            {
                graph = dynamic_cast<viewer::QCPColumnGraph*>(p);
                break;
            }
        }
        if (!graph)
            return;

        double x = graph->dataMainKey(static_cast<int>(dataIndex));
        double y = graph->dataMainValue(static_cast<int>(dataIndex));

        // 创建常驻 tracer 标记（从 StyleManager 读取 permanent 游标样式）
        auto* tracer = new QCPItemTracer(plot);
        tracer->setInterpolating(false);
        {
            auto& ps = m_viewer.GetStyleManager().cursorStyle("permanent");
            tracer->setStyle(QCPItemTracer::tsCircle);
            tracer->setSize(ps.size);
            QColor sc = ps.strokeActive.toQColor();
            tracer->setPen(QPen(sc, ps.strokeWidthActive));
            QColor bc = ps.fillActive.toQColor();
            bc.setAlpha(static_cast<int>(255 * ps.opacityActive));
            tracer->setBrush(bc);
        }
        tracer->position->setCoords(x, y);
        tracer->setVisible(true);
        m_cursorTracers[cursorIdx] = tracer;

        // 创建 QCPItemText 数据标签，位置以 tracer 为父锚点，坐标表示相对 cursor 的像素偏移
        auto* label = new QCPItemText(plot);
        label->position->setType(QCPItemPosition::ptAbsolute);
        label->position->setParentAnchor(tracer->position);
        label->position->setCoords(0, 0);
        label->setText(QString("X: %1\nY: %2")
                           .arg(x, 0, 'g', 8)
                           .arg(y, 0, 'g', 8));
        // label->setFont(QFont("Consolas, Courier New, monospace", 9));  // 已注释：统一使用系统默认字体
        label->setSelectable(false);
        label->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
        label->setPadding(QMargins(6, 3, 6, 3));
        label->setLayer("overlay");
        m_cursorLabels[cursorIdx] = label;
        refreshCursorLabelStyle(cursorIdx, true);  // 新游标 → 激活态

        auto* connector = new QCPItemLine(plot);
        connector->start->setType(QCPItemPosition::ptAbsolute);
        connector->start->setParentAnchor(tracer->position);
        connector->start->setCoords(0, 0);
        connector->end->setType(QCPItemPosition::ptAbsolute);
        connector->setLayer("overlay");
        QPen connectorPen(label->pen().color(), 1.0, Qt::DashLine);
        QColor connectorColor = connectorPen.color();
        connectorColor.setAlpha(180);
        connectorPen.setColor(connectorColor);
        connector->setPen(connectorPen);
        m_cursorConnectorLines[cursorIdx] = connector;
        updateCursorConnectorLine(cursorIdx);

        plot->replot();
    };

    // ---- 游标移除 → 删除 QCPItemText 标签 + tracer ----
    cm.onCursorRemoved = [this](int cursorIdx)
    {
        auto lineIt = m_cursorConnectorLines.find(cursorIdx);
        if (lineIt != m_cursorConnectorLines.end())
        {
            QCPItemLine* line = lineIt.value();
            QCustomPlot* linePlot = line ? line->parentPlot() : nullptr;
            if (linePlot)
                linePlot->removeItem(line);
            m_cursorConnectorLines.erase(lineIt);
            if (linePlot)
                linePlot->replot();
        }

        // 删除 QCPItemText 标签
        auto lit = m_cursorLabels.find(cursorIdx);
        if (lit != m_cursorLabels.end())
        {
            QCPItemText* label = lit.value();
            QCustomPlot* lplot = label ? label->parentPlot() : nullptr;
            if (lplot)
                lplot->removeItem(label);
            m_cursorLabels.erase(lit);
            if (lplot)
                lplot->replot();
        }

        // 删除常驻 tracer
        auto trIt = m_cursorTracers.find(cursorIdx);
        if (trIt != m_cursorTracers.end())
        {
            QCPItemTracer* tracer = trIt.value();
            QCustomPlot* tplot = tracer ? tracer->parentPlot() : nullptr;
            if (tplot)
                tplot->removeItem(tracer);
            m_cursorTracers.erase(trIt);
            if (tplot)
                tplot->replot();
        }

        // 重新编号 labels
        if (!m_cursorLabels.empty())
        {
            QHash<int, QCPItemText*> oldMap = m_cursorLabels;
            m_cursorLabels.clear();
            for (auto oit = oldMap.begin(); oit != oldMap.end(); ++oit)
            {
                int newIdx = (oit.key() > cursorIdx) ? (oit.key() - 1) : oit.key();
                m_cursorLabels[newIdx] = oit.value();
            }
        }

        // 重新编号 tracers
        if (!m_cursorTracers.empty())
        {
            QHash<int, QCPItemTracer*> oldTracers = m_cursorTracers;
            m_cursorTracers.clear();
            for (auto oit = oldTracers.begin(); oit != oldTracers.end(); ++oit)
            {
                int newIdx = (oit.key() > cursorIdx) ? (oit.key() - 1) : oit.key();
                m_cursorTracers[newIdx] = oit.value();
            }
        }

        if (!m_cursorConnectorLines.empty())
        {
            QHash<int, QCPItemLine*> oldLines = m_cursorConnectorLines;
            m_cursorConnectorLines.clear();
            for (auto oit = oldLines.begin(); oit != oldLines.end(); ++oit)
            {
                int newIdx = (oit.key() > cursorIdx) ? (oit.key() - 1) : oit.key();
                m_cursorConnectorLines[newIdx] = oit.value();
                updateCursorConnectorLine(newIdx);
            }
        }
    };

    // ---- 激活游标变更 → 更新标签样式 + tracer 样式 + 内容 ----
    cm.onActiveCursorChanged = [this](int cursorIdx)
    {
        // 更新 QCPItemText 标签的激活/非激活样式
        for (auto it = m_cursorLabels.begin(); it != m_cursorLabels.end(); ++it)
        {
            refreshCursorLabelStyle(it.key(), it.key() == cursorIdx);
        }

        // 更新 tracer 样式（从 StyleManager 读取 permanent 游标样式）
        {
            auto& ps = m_viewer.GetStyleManager().cursorStyle("permanent");
            for (auto it = m_cursorTracers.begin(); it != m_cursorTracers.end(); ++it)
            {
                QCPItemTracer* tr = it.value();
                if (!tr) continue;
                if (it.key() == cursorIdx)
                {
                    QColor bc = ps.fillActive.toQColor();
                    bc.setAlpha(static_cast<int>(255 * ps.opacityActive));
                    tr->setBrush(bc);
                    QColor sc = ps.strokeActive.toQColor();
                    tr->setPen(QPen(sc, ps.strokeWidthActive));
                    tr->setSize(ps.size + 1.0f);  // active size slightly larger
                }
                else
                {
                    QColor bc = ps.fillInactive.toQColor();
                    bc.setAlpha(static_cast<int>(255 * ps.opacityInactive));
                    tr->setBrush(bc);
                    QColor si = ps.strokeInactive.toQColor();
                    if (si.alpha() == 0)
                        tr->setPen(QPen(Qt::NoPen));
                    else
                        tr->setPen(QPen(si, ps.strokeWidthInactive));
                    tr->setSize(ps.size);
                }
            }
        }

        // 更新激活游标的文本和坐标
        if (cursorIdx >= 0)
        {
            const auto& cm = m_viewer.GetCursorManager();
            if (cursorIdx < static_cast<int>(cm.cursors().size()))
            {
                const auto& cursor = cm.cursors()[cursorIdx];
                if (cursor.pageIndex >= 0 && cursor.pageIndex < plotPageCount())
                {
                    auto* container = getPlotContainer(cursor.pageIndex);
                    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
                    if (plot)
                    {
                        viewer::QCPColumnGraph* graph = nullptr;
                        for (int g = 0; g < plot->plottableCount(); ++g)
                        {
                            auto* p = plot->plottable(g);
                            if (p && p->name().toStdString() == cursor.dataItemName)
                            { graph = dynamic_cast<viewer::QCPColumnGraph*>(p); break; }
                        }
                        if (graph)
                        {
                            double x = graph->dataMainKey(static_cast<int>(cursor.dataIndex));
                            double y = graph->dataMainValue(static_cast<int>(cursor.dataIndex));

                            auto trIt = m_cursorTracers.find(cursorIdx);
                            if (trIt != m_cursorTracers.end() && trIt.value())
                                trIt.value()->position->setCoords(x, y);

                            auto lbIt = m_cursorLabels.find(cursorIdx);
                            if (lbIt != m_cursorLabels.end() && lbIt.value())
                            {
                                lbIt.value()->setText(QString("X: %1\nY: %2").arg(x, 0, 'g', 8).arg(y, 0, 'g', 8));
                            }
                            updateCursorConnectorLine(cursorIdx);
                            plot->replot();
                        }
                    }
                }
            }
        }
    };
}

// ============================================================
// refreshCursorLabelStyle: 根据激活状态更新游标 QCPItemText 样式
// ============================================================
void UI::refreshCursorLabelStyle(int cursorIdx, bool active)
{
    auto it = m_cursorLabels.find(cursorIdx);
    if (it == m_cursorLabels.end() || !it.value())
        return;
    auto* label = it.value();

    auto& ds = m_viewer.GetStyleManager().dataBoxStyle();
    QColor fg = ds.textColor.toQColor();
    QColor bg = ds.bgColor.toQColor();
    bg.setAlpha(ds.bgAlpha);

    label->setColor(fg);
    label->setBrush(bg);
    label->setFont(ds.toQFont());
    label->setPadding(QMargins(ds.padLeft, ds.padTop, ds.padRight, ds.padBottom));

    if (active)
    {
        QColor bd = ds.borderActive.toQColor();
        label->setPen(QPen(bd, ds.borderWidthActive));
    }
    else
    {
        QColor bd = ds.borderInactive.toQColor();
        label->setPen(QPen(bd, ds.borderWidthInactive));
    }

    auto lineIt = m_cursorConnectorLines.find(cursorIdx);
    if (lineIt != m_cursorConnectorLines.end() && lineIt.value())
    {
        QPen connectorPen(label->pen().color(), active ? 1.4 : 1.0, Qt::DashLine);
        QColor connectorColor = connectorPen.color();
        connectorColor.setAlpha(active ? 210 : 150);
        connectorPen.setColor(connectorColor);
        lineIt.value()->setPen(connectorPen);
        updateCursorConnectorLine(cursorIdx);
    }

    if (label->parentPlot())
        label->parentPlot()->replot();
}

void UI::updateCursorConnectorLine(int cursorIdx)
{
    auto lineIt = m_cursorConnectorLines.find(cursorIdx);
    auto labelIt = m_cursorLabels.find(cursorIdx);
    auto tracerIt = m_cursorTracers.find(cursorIdx);
    if (lineIt == m_cursorConnectorLines.end() || labelIt == m_cursorLabels.end()
        || tracerIt == m_cursorTracers.end())
        return;

    QCPItemLine* line = lineIt.value();
    QCPItemText* label = labelIt.value();
    QCPItemTracer* tracer = tracerIt.value();
    if (!line || !label || !tracer)
        return;

    QCPItemAnchor* corners[] = {
        label->topLeft,
        label->topRight,
        label->bottomLeft,
        label->bottomRight
    };

    const QPointF cursorPos = tracer->position->pixelPosition();
    QCPItemAnchor* nearest = corners[0];
    double nearestDistance = std::numeric_limits<double>::max();

    for (auto* corner : corners)
    {
        if (!corner)
            continue;

        const QPointF delta = corner->pixelPosition() - cursorPos;
        const double distSq = delta.x() * delta.x() + delta.y() * delta.y();
        if (distSq < nearestDistance)
        {
            nearestDistance = distSq;
            nearest = corner;
        }
    }

    line->end->setParentAnchor(nearest);
    line->end->setCoords(0, 0);
}

QCPItemText* UI::hitCursorLabel(int pageIndex, const QPoint& pos, int* cursorIdx) const
{
    const auto& cursors = m_viewer.GetCursorManager().cursors();

    for (auto it = m_cursorLabels.constBegin(); it != m_cursorLabels.constEnd(); ++it)
    {
        int idx = it.key();
        if (idx < 0 || idx >= static_cast<int>(cursors.size()))
            continue;
        if (cursors[idx].pageIndex != pageIndex)
            continue;

        QCPItemText* label = it.value();
        if (!label || !label->visible())
            continue;

        QCustomPlot* plot = label->parentPlot();
        const double tolerance = plot ? plot->selectionTolerance() : 5.0;
        if (label->selectTest(pos, false, nullptr) <= tolerance)
        {
            if (cursorIdx)
                *cursorIdx = idx;
            return label;
        }
    }

    if (cursorIdx)
        *cursorIdx = -1;
    return nullptr;
}

void UI::cleanupPlotOverlaysBeforeShutdown()
{
    auto removeItem = [](QCPAbstractItem* item)
    {
        if (item && item->parentPlot())
            item->parentPlot()->removeItem(item);
    };

    // Connector lines depend on label/tracer anchors, so remove them first.
    for (auto* line : m_cursorConnectorLines)
        removeItem(line);
    m_cursorConnectorLines.clear();

    for (auto* label : m_cursorLabels)
        removeItem(label);
    m_cursorLabels.clear();

    for (auto* tracer : m_cursorTracers)
        removeItem(tracer);
    m_cursorTracers.clear();

    for (auto* tracer : m_preSelTracers)
        removeItem(tracer);
    m_preSelTracers.clear();

    if (m_fftSelectRect)
    {
        removeItem(m_fftSelectRect);
        m_fftSelectRect = nullptr;
    }

    for (auto it = m_highlightRects.begin(); it != m_highlightRects.end(); ++it)
    {
        for (auto* rect : it.value())
            removeItem(rect);
    }
    m_highlightRects.clear();

    for (auto it = m_highlightLabels.begin(); it != m_highlightLabels.end(); ++it)
    {
        for (auto* label : it.value())
            removeItem(label);
    }
    m_highlightLabels.clear();

    for (auto it = m_highlightReplotConns.begin(); it != m_highlightReplotConns.end(); ++it)
        disconnect(it.value());
    m_highlightReplotConns.clear();

    m_plotToPageIndex.clear();
    m_draggingCursorLabelIdx = -1;
    m_fftSelecting = false;
    m_fftPageIndex = -1;
}
