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
#include <qframe.h>
#include <qboxlayout.h>
#include <qfontmetrics.h>
#include <algorithm>
#include <cmath>
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

extern bool isSystemInDark();

namespace
{
QColor axisCursorColor()
{
    return isSystemInDark() ? QColor(0xd0, 0xd0, 0xd0)
                            : QColor(0x4a, 0x4a, 0x4a);
}

int nearestGraphIndex(viewer::QCPColumnGraph* graph, double x)
{
    if (!graph || graph->dataCount() <= 0)
        return -1;

    const int count = graph->dataCount();
    int index = graph->findBegin(x, false);
    index = std::clamp(index, 0, count - 1);
    if (index > 0)
    {
        const double leftDistance = std::abs(graph->dataMainKey(index - 1) - x);
        const double rightDistance = std::abs(graph->dataMainKey(index) - x);
        if (leftDistance <= rightDistance)
            --index;
    }
    return index;
}
}

// ============================================================
// CursorManager → Qt 控件绑定
// ============================================================

void UI::bindCursorManagerCallbacks()
{
    logOperationTrace("bind cursor manager callbacks enter");
    auto& cm = m_viewer.GetCursorManager();
    auto& pm = m_viewer.GetPlotManager();

    // ---- 扩展 onPageAdded：在 plot 创建后附加 tracer ----
    auto originalOnPageAdded = pm.onPageAdded;

    pm.onPageAdded = [this, originalOnPageAdded](int index)
    {
        logOperationTrace(QString("cursor page wrapper enter page=%1").arg(index));
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
        auto repositionAxisCursors = [this, index]()
        {
            const auto& cursors = m_viewer.GetCursorManager().axisCursors();
            for (const auto& cursor : cursors)
            {
                if (cursor.pageIndex == index)
                    updateAxisCursorVisual(cursor.id, cursor);
            }
        };
        connect(plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, [repositionAxisCursors](const QCPRange&) { repositionAxisCursors(); });
        connect(plot->yAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, [repositionAxisCursors](const QCPRange&) { repositionAxisCursors(); });
        logOperationTrace(QString("cursor page wrapper leave page=%1 plot=0x%2 preselectionTracer=0x%3")
                          .arg(index).arg(reinterpret_cast<quintptr>(plot), 0, 16)
                          .arg(reinterpret_cast<quintptr>(tracer), 0, 16));
    };

    // ---- 扩展 onPageAboutToRemove：清理该页面的 tracer 和相关游标 ----
    auto originalOnPageAboutToRemove = pm.onPageAboutToRemove;

    pm.onPageAboutToRemove = [this, originalOnPageAboutToRemove](int index)
    {
        logShutdownTrace(QString("cursor-wrapper onPageAboutToRemove enter index=%1 cursorCount=%2")
                             .arg(index)
                             .arg(static_cast<int>(m_viewer.GetCursorManager().cursors().size())));
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
        cm.removeAxisCursors(index);

        // 清除可能存在的预选
        cm.clearPreSelection();

        // ---- 清理高亮元素映射 ----
        m_highlightRects.remove(index);
        m_highlightLines.remove(index);
        m_highlightLabels.remove(index);

        // 执行原有逻辑（删除 widget）
        if (originalOnPageAboutToRemove)
            originalOnPageAboutToRemove(index);
        logShutdownTrace(QString("cursor-wrapper onPageAboutToRemove leave index=%1").arg(index));
    };

    // ---- 扩展 onAboutToClear：数据仍有效时先清理所有 cursor 状态 ----
    auto originalOnAboutToClear = pm.onAboutToClear;
    pm.onAboutToClear = [this, originalOnAboutToClear]()
    {
        logShutdownTrace(QString("cursor-wrapper onAboutToClear enter cursorCount=%1")
                             .arg(static_cast<int>(m_viewer.GetCursorManager().cursors().size())));
        // 清理期间的每个游标删除都不需要立即重绘；图窗马上会被删除。
        m_clearingAllPlots = true;
        auto& cm = m_viewer.GetCursorManager();
        cm.clearAll();
        m_plotToPageIndex.clear();
        m_preSelTracers.clear();
        m_cursorTracers.clear();
        m_cursorLabels.clear();
        m_cursorConnectorLines.clear();
        m_axisCursorVisuals.clear();
        m_axisCursorValueBoxes.clear();
        m_highlightRects.clear();
        m_highlightLines.clear();
        m_highlightLabels.clear();
        m_draggingCursorLabelIdx = -1;
        m_draggingAxisCursorId = -1;
        m_draggingAxisCursorValueBoxId = -1;

        if (originalOnAboutToClear)
            originalOnAboutToClear();
        logShutdownTrace("cursor-wrapper onAboutToClear leave");
    };

    // Dock 和 UI 状态全部清理完成后再退出批量清理状态。
    auto originalOnCleared = pm.onCleared;
    pm.onCleared = [this, originalOnCleared]()
    {
        if (originalOnCleared)
            originalOnCleared();
        m_clearingAllPlots = false;
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
                if (plot && !m_clearingAllPlots)
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
        logOperationTrace(QString("cursor add callback enter cursor=%1 page=%2 item=\"%3\" dataIndex=%4")
                          .arg(cursorIdx).arg(pageIndex).arg(QString::fromStdString(dataItemName))
                          .arg(dataIndex));
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
        logOperationTrace(QString("cursor add callback leave cursor=%1 page=%2 tracer=0x%3 label=0x%4 connector=0x%5")
                          .arg(cursorIdx).arg(pageIndex)
                          .arg(reinterpret_cast<quintptr>(tracer), 0, 16)
                          .arg(reinterpret_cast<quintptr>(label), 0, 16)
                          .arg(reinterpret_cast<quintptr>(connector), 0, 16));
    };

    // ---- 游标移除 → 删除 QCPItemText 标签 + tracer ----
    cm.onCursorRemoved = [this](int cursorIdx)
    {
        logOperationTrace(QString("cursor remove callback enter cursor=%1 labels=%2 tracers=%3 connectors=%4")
                          .arg(cursorIdx).arg(m_cursorLabels.size())
                          .arg(m_cursorTracers.size()).arg(m_cursorConnectorLines.size()));
        auto lineIt = m_cursorConnectorLines.find(cursorIdx);
        if (lineIt != m_cursorConnectorLines.end())
        {
            QCPItemLine* line = lineIt.value();
            QCustomPlot* linePlot = line ? line->parentPlot() : nullptr;
            if (linePlot)
                linePlot->removeItem(line);
            m_cursorConnectorLines.erase(lineIt);
            if (linePlot && !m_clearingAllPlots)
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
            if (lplot && !m_clearingAllPlots)
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
            if (tplot && !m_clearingAllPlots)
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
        logOperationTrace(QString("cursor remove callback leave cursor=%1 labels=%2 tracers=%3 connectors=%4")
                          .arg(cursorIdx).arg(m_cursorLabels.size())
                          .arg(m_cursorTracers.size()).arg(m_cursorConnectorLines.size()));
    };

    // ---- 激活游标变更 → 更新标签样式 + tracer 样式 + 内容 ----
    cm.onActiveCursorChanged = [this](int cursorIdx)
    {
        logOperationTrace(QString("active cursor changed cursor=%1 cursorCount=%2")
                          .arg(cursorIdx).arg(m_viewer.GetCursorManager().cursors().size()));
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

    cm.onAxisCursorAdded = [this](int id, const viewer::AxisCursorInfo& cursor)
    {
        if (cursor.pageIndex < 0 || cursor.pageIndex >= plotPageCount())
            return;

        QCustomPlot* plot = getPlot(cursor.pageIndex);
        if (!plot)
            return;

        AxisCursorVisual visual;
        visual.line = new QCPItemStraightLine(plot);
        visual.line->setLayer("overlay");
        visual.line->setSelectable(false);
        visual.line->setClipToAxisRect(true);
        visual.line->setClipAxisRect(plot->axisRect());

        if (cursor.type == viewer::AxisCursorType::X)
        {
            visual.valueBoxConnector = new QCPItemLine(plot);
            visual.valueBoxConnector->start->setType(QCPItemPosition::ptAbsolute);
            visual.valueBoxConnector->end->setType(QCPItemPosition::ptAbsolute);
            visual.valueBoxConnector->setLayer("overlay");
            visual.valueBoxConnector->setSelectable(false);
            visual.valueBoxConnector->setClipToAxisRect(true);
            visual.valueBoxConnector->setClipAxisRect(plot->axisRect());

            visual.valueBox = new QFrame(plot);
            visual.valueBox->setObjectName(QStringLiteral("axisCursorValueBox"));
            visual.valueBox->setAttribute(Qt::WA_StyledBackground, true);
            visual.valueBox->setCursor(Qt::OpenHandCursor);
            visual.valueBox->installEventFilter(this);
            auto* layout = new QVBoxLayout(visual.valueBox);
            layout->setContentsMargins(6, 4, 6, 4);
            layout->setSpacing(1);
            layout->setSizeConstraint(QLayout::SetFixedSize);
            m_axisCursorValueBoxes.insert(visual.valueBox, id);
        }

        m_axisCursorVisuals.insert(id, visual);
        updateAxisCursorVisual(id, cursor);
        refreshAxisCursorTheme();
        plot->replot(QCustomPlot::rpQueuedRefresh);
    };

    cm.onAxisCursorRemoved = [this](int id)
    {
        auto it = m_axisCursorVisuals.find(id);
        if (it == m_axisCursorVisuals.end())
            return;

        QCustomPlot* plot = it->line ? it->line->parentPlot() : nullptr;
        if (it->valueBox)
        {
            m_axisCursorValueBoxes.remove(it->valueBox);
            it->valueBox->removeEventFilter(this);
            it->valueBox->hide();
            it->valueBox->deleteLater();
        }
        if (plot && it->valueBoxConnector)
            plot->removeItem(it->valueBoxConnector);
        if (plot && it->line)
            plot->removeItem(it->line);
        m_axisCursorVisuals.erase(it);

        if (m_draggingAxisCursorId == id)
            m_draggingAxisCursorId = -1;
        if (m_draggingAxisCursorValueBoxId == id)
            m_draggingAxisCursorValueBoxId = -1;
        if (plot && !m_clearingAllPlots)
            plot->replot(QCustomPlot::rpQueuedRefresh);
    };

    cm.onAxisCursorChanged = [this](int id, const viewer::AxisCursorInfo& cursor)
    {
        updateAxisCursorVisual(id, cursor);
        if (auto it = m_axisCursorVisuals.find(id);
            it != m_axisCursorVisuals.end() && it->line && it->line->parentPlot())
        {
            it->line->parentPlot()->replot(QCustomPlot::rpQueuedRefresh);
        }
    };

    cm.onActiveAxisCursorChanged = [this](int activeId)
    {
        const QColor color = axisCursorColor();
        for (auto it = m_axisCursorVisuals.begin(); it != m_axisCursorVisuals.end(); ++it)
        {
            if (it->line)
            {
                it->line->setPen(QPen(color, it.key() == activeId ? 2.0 : 1.2));
                if (it->line->parentPlot())
                    it->line->parentPlot()->replot(QCustomPlot::rpQueuedRefresh);
            }
        }
    };
    logOperationTrace("bind cursor manager callbacks leave");
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

void UI::createAxisCursor(int pageIndex, viewer::AxisCursorType type, double position)
{
    QCustomPlot* plot = getPlot(pageIndex);
    if (!plot || !plot->axisRect())
        return;

    const QCPRange range = (type == viewer::AxisCursorType::X)
        ? plot->xAxis->range() : plot->yAxis->range();
    position = std::clamp(position, range.lower, range.upper);

    auto& manager = m_viewer.GetCursorManager();
    manager.setActiveCursor(-1);
    manager.addAxisCursor(pageIndex, type, position);
}

void UI::updateAxisCursorVisual(int id, const viewer::AxisCursorInfo& cursor)
{
    auto visualIt = m_axisCursorVisuals.find(id);
    if (visualIt == m_axisCursorVisuals.end() || !visualIt->line)
        return;

    QCustomPlot* plot = visualIt->line->parentPlot();
    if (!plot)
        return;

    visualIt->line->point1->setType(QCPItemPosition::ptPlotCoords);
    visualIt->line->point2->setType(QCPItemPosition::ptPlotCoords);
    visualIt->line->point1->setAxes(plot->xAxis, plot->yAxis);
    visualIt->line->point2->setAxes(plot->xAxis, plot->yAxis);

    if (cursor.type == viewer::AxisCursorType::X)
    {
        const QCPRange yRange = plot->yAxis->range();
        visualIt->line->point1->setCoords(cursor.position, yRange.lower);
        visualIt->line->point2->setCoords(cursor.position, yRange.upper);
        refreshAxisCursorValueBox(id);
    }
    else
    {
        const QCPRange xRange = plot->xAxis->range();
        visualIt->line->point1->setCoords(xRange.lower, cursor.position);
        visualIt->line->point2->setCoords(xRange.upper, cursor.position);
    }
}

void UI::refreshAxisCursorValueBox(int id, bool resetToBottom)
{
    const viewer::AxisCursorInfo* cursor = m_viewer.GetCursorManager().axisCursor(id);
    auto visualIt = m_axisCursorVisuals.find(id);
    if (!cursor || cursor->type != viewer::AxisCursorType::X
        || visualIt == m_axisCursorVisuals.end() || !visualIt->valueBox)
    {
        return;
    }

    QFrame* box = visualIt->valueBox;
    if (resetToBottom)
        visualIt->valueBoxAtDefaultBottom = true;
    if (!cursor->dataValuesVisible)
    {
        box->hide();
        if (visualIt->valueBoxConnector)
            visualIt->valueBoxConnector->setVisible(false);
        return;
    }

    QCustomPlot* plot = visualIt->line ? visualIt->line->parentPlot() : nullptr;
    if (!plot)
        return;

    QStringList texts;
    QList<QColor> colors;
    texts.push_back(QStringLiteral("X: %1").arg(cursor->position, 0, 'g', 12));
    colors.push_back(axisCursorColor());

    for (int i = 0; i < plot->plottableCount(); ++i)
    {
        auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
        const int dataIndex = nearestGraphIndex(graph, cursor->position);
        if (!graph || dataIndex < 0)
            continue;

        texts.push_back(QStringLiteral("%1: %2")
                            .arg(graph->name())
                            .arg(graph->dataMainValue(dataIndex), 0, 'g', 12));
        QColor color = graph->pen().color();
        if (!color.isValid())
            color = axisCursorColor();
        colors.push_back(color);
    }

    auto* layout = qobject_cast<QVBoxLayout*>(box->layout());
    if (!layout)
        return;

    while (visualIt->valueLabels.size() > texts.size())
    {
        QLabel* label = visualIt->valueLabels.takeLast();
        layout->removeWidget(label);
        delete label;
    }
    while (visualIt->valueLabels.size() < texts.size())
    {
        auto* label = new QLabel(box);
        label->setTextFormat(Qt::PlainText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        label->setMargin(0);
        layout->addWidget(label);
        visualIt->valueLabels.push_back(label);
    }

    const QFont valueFont = m_viewer.GetStyleManager().dataBoxStyle().toQFont();
    for (qsizetype i = 0; i < texts.size(); ++i)
    {
        QLabel* label = visualIt->valueLabels[i];
        label->setFont(valueFont);
        label->setText(texts[i]);
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                 .arg(colors[i].name(QColor::HexArgb)));
    }

    layout->activate();
    box->adjustSize();
    box->show();
    box->raise();
    positionAxisCursorValueBox(id);
}

void UI::refreshAxisCursorValueBoxes(int pageIndex)
{
    const auto& cursors = m_viewer.GetCursorManager().axisCursors();
    for (const auto& cursor : cursors)
    {
        if (cursor.pageIndex == pageIndex && cursor.type == viewer::AxisCursorType::X)
            refreshAxisCursorValueBox(cursor.id);
    }
}

void UI::refreshAxisCursorTheme()
{
    const QColor color = axisCursorColor();
    const bool dark = isSystemInDark();
    const int activeId = m_viewer.GetCursorManager().activeAxisCursorId();

    for (auto it = m_axisCursorVisuals.begin(); it != m_axisCursorVisuals.end(); ++it)
    {
        if (it->line)
            it->line->setPen(QPen(color, it.key() == activeId ? 2.0 : 1.2));
        if (it->valueBoxConnector)
        {
            QColor connectorColor = color;
            connectorColor.setAlpha(180);
            it->valueBoxConnector->setPen(
                QPen(connectorColor, 1.0, Qt::DashLine));
        }
        if (it->valueBox)
        {
            it->valueBox->setStyleSheet(dark
                ? QStringLiteral("QFrame#axisCursorValueBox { background-color: rgba(45,45,45,230); border: 1px solid #d0d0d0; border-radius: 3px; }")
                : QStringLiteral("QFrame#axisCursorValueBox { background-color: rgba(245,245,245,235); border: 1px solid #4a4a4a; border-radius: 3px; }"));
            refreshAxisCursorValueBox(it.key());
        }
    }
}

void UI::positionAxisCursorValueBox(int id)
{
    const viewer::AxisCursorInfo* cursor = m_viewer.GetCursorManager().axisCursor(id);
    auto visualIt = m_axisCursorVisuals.find(id);
    if (!cursor || cursor->type != viewer::AxisCursorType::X
        || visualIt == m_axisCursorVisuals.end() || !visualIt->valueBox
        || !visualIt->line || !visualIt->line->parentPlot())
    {
        return;
    }

    QCustomPlot* plot = visualIt->line->parentPlot();
    const QRect rect = plot->axisRect()->rect();
    QFrame* box = visualIt->valueBox;
    const QPoint anchor(qRound(plot->xAxis->coordToPixel(cursor->position)), rect.bottom());

    if (visualIt->valueBoxAtDefaultBottom)
    {
        visualIt->valueBoxOffset = QPoint(-box->width() / 2, -box->height() - 6);
    }

    QPoint target = anchor + visualIt->valueBoxOffset;
    const int maxX = std::max(rect.left(), rect.right() - box->width() + 1);
    const int maxY = std::max(rect.top(), rect.bottom() - box->height() + 1);
    target.setX(std::clamp(target.x(), rect.left(), maxX));
    target.setY(std::clamp(target.y(), rect.top(), maxY));
    box->move(target);
    updateAxisCursorValueBoxConnector(id);
}

void UI::updateAxisCursorValueBoxConnector(int id)
{
    const viewer::AxisCursorInfo* cursor = m_viewer.GetCursorManager().axisCursor(id);
    auto visualIt = m_axisCursorVisuals.find(id);
    if (!cursor || cursor->type != viewer::AxisCursorType::X
        || visualIt == m_axisCursorVisuals.end()
        || !visualIt->line || !visualIt->line->parentPlot()
        || !visualIt->valueBox || !visualIt->valueBoxConnector)
    {
        return;
    }

    QCustomPlot* plot = visualIt->line->parentPlot();
    QFrame* box = visualIt->valueBox;
    QCPItemLine* connector = visualIt->valueBoxConnector;
    const bool visible = cursor->dataValuesVisible && !box->isHidden();
    connector->setVisible(visible);
    if (!visible || !plot->axisRect())
        return;

    const QRect axisRect = plot->axisRect()->rect();
    const QPointF cursorBottom(plot->xAxis->coordToPixel(cursor->position),
                               axisRect.bottom());
    const QRect boxRect(box->pos(), box->size());
    const QPointF corners[] = {
        boxRect.topLeft(), boxRect.topRight(),
        boxRect.bottomLeft(), boxRect.bottomRight()
    };

    QPointF nearest = corners[0];
    qreal nearestDistance = std::numeric_limits<qreal>::max();
    for (const QPointF& corner : corners)
    {
        const QPointF delta = corner - cursorBottom;
        const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = corner;
        }
    }

    connector->start->setCoords(cursorBottom.x(), cursorBottom.y());
    connector->end->setCoords(nearest.x(), nearest.y());
}

int UI::hitAxisCursor(int pageIndex, const QPoint& pos, double tolerance) const
{
    QCustomPlot* plot = getPlot(pageIndex);
    if (!plot || !plot->axisRect() || !plot->axisRect()->rect().contains(pos))
        return -1;

    int bestId = -1;
    double bestDistance = tolerance;
    for (const auto& cursor : m_viewer.GetCursorManager().axisCursors())
    {
        if (cursor.pageIndex != pageIndex)
            continue;

        const double pixel = (cursor.type == viewer::AxisCursorType::X)
            ? plot->xAxis->coordToPixel(cursor.position)
            : plot->yAxis->coordToPixel(cursor.position);
        const double distance = std::abs(pixel - (cursor.type == viewer::AxisCursorType::X
                                                    ? pos.x() : pos.y()));
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestId = cursor.id;
        }
    }
    return bestId;
}

void UI::showAxisCursorObjectMenu(int id, const QPoint& globalPos)
{
    const viewer::AxisCursorInfo* cursor = m_viewer.GetCursorManager().axisCursor(id);
    if (!cursor)
        return;

    const bool isXCursor = cursor->type == viewer::AxisCursorType::X;
    const bool valuesVisible = cursor->dataValuesVisible;
    QMenu menu;
    QAction* deleteAction = menu.addAction(QString::fromUtf8("删除"));
    QAction* valuesAction = nullptr;
    if (isXCursor)
    {
        valuesAction = menu.addAction(valuesVisible
            ? QString::fromUtf8("隐藏数据值") : QString::fromUtf8("显示数据值"));
    }

    connect(deleteAction, &QAction::triggered, this, [this, id]()
    {
        m_viewer.GetCursorManager().removeAxisCursor(id);
    });
    if (valuesAction)
    {
        connect(valuesAction, &QAction::triggered, this, [this, id, valuesVisible]()
        {
            if (!valuesVisible)
            {
                auto it = m_axisCursorVisuals.find(id);
                if (it != m_axisCursorVisuals.end())
                    it->valueBoxAtDefaultBottom = true;
            }
            m_viewer.GetCursorManager().setAxisCursorDataValuesVisible(id, !valuesVisible);
        });
    }
    menu.exec(globalPos);
}

bool UI::showAxisCursorContextMenu(int pageIndex, QCustomPlot* plot, const QPoint& pos)
{
    const int id = hitAxisCursor(pageIndex, pos);
    if (id < 0 || !plot)
        return false;

    m_viewer.GetCursorManager().setActiveCursor(-1);
    m_viewer.GetCursorManager().setActiveAxisCursor(id);
    showAxisCursorObjectMenu(id, plot->mapToGlobal(pos));
    return true;
}

void UI::cleanupPlotOverlaysBeforeShutdown()
{
    logShutdownTrace(QString("cleanupPlotOverlaysBeforeShutdown enter labels=%1 tracers=%2 preSel=%3 lines=%4 axisCursors=%5 highlightRects=%6 highlightLines=%7 highlightLabels=%8")
                         .arg(m_cursorLabels.size())
                         .arg(m_cursorTracers.size())
                         .arg(m_preSelTracers.size())
                         .arg(m_cursorConnectorLines.size())
                         .arg(m_axisCursorVisuals.size())
                         .arg(m_highlightRects.size())
                         .arg(m_highlightLines.size())
                         .arg(m_highlightLabels.size()));

    m_cursorConnectorLines.clear();

    m_cursorLabels.clear();

    m_cursorTracers.clear();

    m_preSelTracers.clear();

    m_axisCursorValueBoxes.clear();
    m_axisCursorVisuals.clear();

    m_fftSelectRect = nullptr;

    m_highlightRects.clear();

    m_highlightLines.clear();

    m_highlightLabels.clear();

    for (auto it = m_highlightReplotConns.begin(); it != m_highlightReplotConns.end(); ++it)
        disconnect(it.value());
    m_highlightReplotConns.clear();

    m_plotToPageIndex.clear();
    m_draggingCursorLabelIdx = -1;
    m_draggingAxisCursorId = -1;
    m_draggingAxisCursorValueBoxId = -1;
    m_fftSelecting = false;
    m_fftPageIndex = -1;
    logShutdownTrace("cleanupPlotOverlaysBeforeShutdown leave");
}
