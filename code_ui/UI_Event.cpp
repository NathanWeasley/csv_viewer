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
    // 右键上下文菜单（框选模式 / FFT 框选取消）
    // ============================================================
    if (event->type() == QEvent::ContextMenu)
    {
        if (plot)
        {
            // FFT 框选模式中，右键取消框选
            if (m_fftSelecting && m_fftPageIndex >= 0)
            {
                auto* container = getPlotContainer(m_fftPageIndex);
                auto* fftPlot = container ? container->findChild<QCustomPlot*>() : nullptr;
                if (fftPlot == plot)
                {
                    cancelFFTSelection();
                    return true;
                }
            }

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
    // FFT 框选模式下独占鼠标事件，放行 Paint 等内部事件
    // ============================================================
    if (plot && m_fftSelecting && m_fftPageIndex >= 0)
    {
        auto* container = getPlotContainer(m_fftPageIndex);
        auto* fftPlot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (fftPlot == plot)
        {
            if (event->type() == QEvent::MouseButtonPress)
            {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    m_fftSelStart = me->pos();
                    m_fftSelEnd = me->pos();
                    if (m_fftSelectRect)
                    {
                        m_fftSelectRect->topLeft->setCoords(
                            plot->xAxis->pixelToCoord(m_fftSelStart.x()),
                            plot->yAxis->pixelToCoord(m_fftSelStart.y()));
                        m_fftSelectRect->bottomRight->setCoords(
                            plot->xAxis->pixelToCoord(m_fftSelEnd.x()),
                            plot->yAxis->pixelToCoord(m_fftSelEnd.y()));
                        m_fftSelectRect->setVisible(true);
                        plot->replot();
                    }
                    return true;
                }
            }

            if (event->type() == QEvent::MouseMove)
            {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->buttons() & Qt::LeftButton)
                {
                    m_fftSelEnd = me->pos();
                    if (m_fftSelectRect)
                    {
                        m_fftSelectRect->topLeft->setCoords(
                            plot->xAxis->pixelToCoord(m_fftSelStart.x()),
                            plot->yAxis->pixelToCoord(m_fftSelStart.y()));
                        m_fftSelectRect->bottomRight->setCoords(
                            plot->xAxis->pixelToCoord(m_fftSelEnd.x()),
                            plot->yAxis->pixelToCoord(m_fftSelEnd.y()));
                        plot->replot();
                    }
                }
                return true;
            }

            if (event->type() == QEvent::MouseButtonRelease)
            {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton)
                {
                    double x1 = plot->xAxis->pixelToCoord(m_fftSelStart.x());
                    double x2 = plot->xAxis->pixelToCoord(m_fftSelEnd.x());
                    double xMin = std::min(x1, x2);
                    double xMax = std::max(x1, x2);

                    if (m_fftSelectRect)
                    {
                        plot->removeItem(m_fftSelectRect);
                        m_fftSelectRect = nullptr;
                    }
                    m_fftSelecting = false;
                    plot->setCursor(Qt::ArrowCursor);
                    plot->replot();

                    int pageIdx = m_fftPageIndex;
                    m_fftPageIndex = -1;
                    showFFTDialog(pageIdx, xMin, xMax);
                    return true;
                }
            }

            // 对输入事件阻止穿透；Paint/Resize 等内部事件放行
            switch (event->type())
            {
            case QEvent::MouseMove:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::Wheel:
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
                return true;
            default:
                break;
            }
            // Paint / Resize 等放行，否则 plot 白屏
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
    // 使用二分查找 O(log N) 定位最近数据点，不受自适应降采样影响
    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);

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

        return QMainWindow::eventFilter(obj, event);
    }

    // ---- MouseButtonPress: 记录按下位置 ----
    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        m_mousePressPos = me->pos();
        m_mousePressOnPlot = true;

        plot->setFocusPolicy(Qt::StrongFocus);
        plot->setFocus();

        if (me->modifiers() & Qt::ShiftModifier)
            return true;

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
                return true;
            }
        }

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
            return QMainWindow::eventFilter(obj, event);
        }

        bool shiftHeld = (me->modifiers() & Qt::ShiftModifier);

        const double cursorHitRadius = 10.0;
        const auto& cursors = cm.cursors();
        int hitCursorIdx = -1;
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
            cm.addCursor(pageIndex, cm.preSelItem(), cm.preSelIndex());
            return true;
        }
        else
        {
            cm.setActiveCursor(-1);
            cm.clearPreSelection();
            return QMainWindow::eventFilter(obj, event);
        }
    }

    // ---- KeyPress: Delete / 方向键 / Escape ----
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);

        if (ke->key() == Qt::Key_Escape)
        {
            if (m_fftSelecting)
            {
                cancelFFTSelection();
                return true;
            }
        }

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