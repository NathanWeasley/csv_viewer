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

// ============================================================
// 事件过滤器：Ctrl/Shift + 滚轮单轴缩放 / 游标交互 / Delete
// ============================================================

bool UI::eventFilter(QObject* obj, QEvent* event)
{
    QCustomPlot* plot = qobject_cast<QCustomPlot*>(obj);

    // ============================================================
    // 右键上下文菜单（框选模式）
    // ============================================================
    if (event->type() == QEvent::ContextMenu)
    {
        if (plot)
        {
            int pageIndex = m_plotToPageIndex.value(plot, -1);
            if (pageIndex >= 0 && m_viewer.GetPlotManager().isRectZoomActive(pageIndex))
            {
                m_viewer.GetPlotManager().setRectZoomActive(pageIndex, false);
                return true;
            }
        }
    }

    // ============================================================
    // 滚轮缩放
    // ============================================================
    if (event->type() == QEvent::Wheel)
    {
        if (plot)
        {
            QWheelEvent* we = static_cast<QWheelEvent*>(event);
            Qt::KeyboardModifiers mods = we->modifiers();

            if (mods & Qt::ControlModifier)
            {
                plot->axisRect()->setRangeZoom(Qt::Horizontal);
                plot->removeEventFilter(this);
                QCoreApplication::sendEvent(plot, event);
                plot->installEventFilter(this);
                plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
                return true;
            }
            else if (mods & Qt::ShiftModifier)
            {
                plot->axisRect()->setRangeZoom(Qt::Vertical);
                plot->removeEventFilter(this);
                QCoreApplication::sendEvent(plot, event);
                plot->installEventFilter(this);
                plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
                return true;
            }
        }
    }

    // ============================================================
    // 游标交互事件（仅 QCustomPlot）
    // ============================================================
    if (!plot)
        return QMainWindow::eventFilter(obj, event);

    int pageIndex = m_plotToPageIndex.value(plot, -1);
    if (pageIndex < 0)
        return QMainWindow::eventFilter(obj, event);

    auto& cm = m_viewer.GetCursorManager();

    // ---- MouseMove: 预选检测 ----
    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);

        // 密度检查：数据点数/像素宽度 > 1 时不启用预选
        if (isDataTooDense(plot))
        {
            cm.clearPreSelection();
            return QMainWindow::eventFilter(obj, event);
        }

        auto result = findNearestDataPoint(plot, me->pos(), 10.0);
        viewer::QCPColumnGraph* graph = result.first;
        size_t dataIdx = result.second;

        if (graph)
        {
            std::string name = graph->name().toStdString();
            cm.setPreSelection(pageIndex, name, dataIdx);
        }
        else
        {
            cm.clearPreSelection();
        }

        // 让 QCustomPlot 继续处理拖拽等相关事件
        return QMainWindow::eventFilter(obj, event);
    }

    // ---- MouseButtonPress: 记录按下位置 ----
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        m_mousePressPos = me->pos();
        m_mousePressOnPlot = true;

        // KeyPress（Delete）需要焦点，设置 plot 可获焦
        plot->setFocusPolicy(Qt::StrongFocus);
        plot->setFocus();

        // Shift 按下时不传递给 QCustomPlot，避免启动拖拽导致粘滞
        if (me->modifiers() & Qt::ShiftModifier)
            return true;

        // 点击在已有游标附近 → 阻止 QCP 拖拽启动
        const double cursorHitRadius = 10.0;
        const auto& cursors = cm.cursors();
        for (size_t i = 0; i < cursors.size(); ++i)
        {
            if (cursors[i].pageIndex != pageIndex)
                continue;
            viewer::QCPColumnGraph* cg = nullptr;
            for (int g = 0; g < plot->plottableCount(); ++g)
            {
                auto* p = plot->plottable(g);
                if (p && p->name().toStdString() == cursors[i].dataItemName)
                {
                    cg = dynamic_cast<viewer::QCPColumnGraph*>(p);
                    break;
                }
            }
            if (!cg) continue;
            QPointF cpos = cg->dataPixelPosition(static_cast<int>(cursors[i].dataIndex));
            double dx = cpos.x() - me->pos().x();
            double dy = cpos.y() - me->pos().y();
            if ((dx * dx + dy * dy) < (cursorHitRadius * cursorHitRadius))
            {
                return true;  // 阻止 QCP 拖拽
            }
        }

        // 让 QCustomPlot 继续处理拖拽/缩放
        return QMainWindow::eventFilter(obj, event);
    }

    // ---- MouseButtonRelease: 判断单击 or 拖拽 ----
    if (event->type() == QEvent::MouseButtonRelease)
    {
        if (!m_mousePressOnPlot)
            return QMainWindow::eventFilter(obj, event);

        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        m_mousePressOnPlot = false;

        QPoint delta = me->pos() - m_mousePressPos;
        bool isClick = (delta.manhattanLength() < 3);

        if (!isClick)
        {
            // 拖拽操作，让 QCustomPlot 自己处理
            return QMainWindow::eventFilter(obj, event);
        }

        // ---- 单击处理 ----
        bool shiftHeld = (me->modifiers() & Qt::ShiftModifier);

        // 优先：点击已有游标附近 → 激活该游标
        const double cursorHitRadius = 10.0;
        const auto& cursors = cm.cursors();
        int hitCursorIdx = -1;
        for (size_t i = 0; i < cursors.size(); ++i)
        {
            if (cursors[i].pageIndex != pageIndex)
                continue;

            // 查找对应 graph
            viewer::QCPColumnGraph* cg = nullptr;
            for (int g = 0; g < plot->plottableCount(); ++g)
            {
                auto* p = plot->plottable(g);
                if (p && p->name().toStdString() == cursors[i].dataItemName)
                {
                    cg = dynamic_cast<viewer::QCPColumnGraph*>(p);
                    break;
                }
            }
            if (!cg) continue;

            QPointF cpos = cg->dataPixelPosition(static_cast<int>(cursors[i].dataIndex));
            double dx = cpos.x() - me->pos().x();
            double dy = cpos.y() - me->pos().y();
            if ((dx * dx + dy * dy) < (cursorHitRadius * cursorHitRadius))
            {
                hitCursorIdx = static_cast<int>(i);
                break;
            }
        }

        if (hitCursorIdx >= 0)
        {
            cm.setActiveCursor(hitCursorIdx);
            return true;
        }

        if (shiftHeld && cm.hasPreSelection()
            && cm.preSelPage() == pageIndex)
        {
            // Shift+单击预选点 → 添加游标
            cm.addCursor(pageIndex, cm.preSelItem(), cm.preSelIndex());
            return true; // 消费事件
        }
        else
        {
            // 空白处单击 → 解除所有游标激活
            cm.setActiveCursor(-1);
            cm.clearPreSelection();
            return QMainWindow::eventFilter(obj, event);
        }
    }

    // ---- KeyPress: Delete / 方向键 ----
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete)
        {
            if (cm.hasActiveCursor())
            {
                cm.removeActiveCursor();
                return true;
            }
        }
        else if (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right)
        {
            if (!cm.hasActiveCursor())
                return QMainWindow::eventFilter(obj, event);

            int activeIdx = cm.activeCursorIndex();
            const auto& cursor = cm.cursors()[activeIdx];
            if (cursor.pageIndex != pageIndex)
                return QMainWindow::eventFilter(obj, event);

            // 查找对应 graph
            viewer::QCPColumnGraph* cg = nullptr;
            for (int g = 0; g < plot->plottableCount(); ++g)
            {
                auto* p = plot->plottable(g);
                if (p && p->name().toStdString() == cursor.dataItemName)
                {
                    cg = dynamic_cast<viewer::QCPColumnGraph*>(p);
                    break;
                }
            }
            if (!cg || cg->dataCount() == 0)
                return true;

            int delta = (ke->key() == Qt::Key_Left) ? -1 : 1;
            int newIdx = static_cast<int>(cursor.dataIndex) + delta;
            if (newIdx < 0) newIdx = 0;
            if (newIdx >= cg->dataCount()) newIdx = cg->dataCount() - 1;

            cm.moveCursorToIndex(activeIdx, static_cast<size_t>(newIdx));
            return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}
