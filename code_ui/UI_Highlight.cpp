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
// showHighlightDialog: 显示高亮规则配置对话框
// ============================================================

void UI::showHighlightDialog(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
        return;

    auto& pm = m_viewer.GetPlotManager();
    auto& dm = m_viewer.GetDataManager();

    // 获取所有列名
    const auto& colNames = dm.GetColumnNames();
    std::vector<std::string> columns(colNames.begin(), colNames.end());

    // 创建对话框
    HighlightDialog dlg(columns, this);

    // 设置已有规则
    auto& hlMgr = pm.pageInfo(pageIndex).highlightMgr;
    dlg.setRules(hlMgr.rules());

    if (dlg.exec() != QDialog::Accepted)
        return;

    // 写入规则
    auto newRules = dlg.getRules();
    hlMgr.clearAll();
    for (const auto& rule : newRules)
        hlMgr.addRule(rule);

    // 渲染高亮
    renderHighlights(pageIndex);
}

// ============================================================
// renderHighlights: 根据高亮规则绘制色块和文字标注
// ============================================================

void UI::renderHighlights(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
        return;

    auto* container = m_plotTabs->widget(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
        return;

    // ---- 确保 highlight 层存在（位于 grid 层之下） ----
    if (!plot->layer("highlight"))
        plot->addLayer("highlight", plot->layer("grid"), QCustomPlot::limBelow);

    // ---- 清除旧的高亮元素 ----
    auto& rects = m_highlightRects[pageIndex];
    for (auto* rect : rects)
    {
        if (rect && rect->parentPlot())
            rect->parentPlot()->removeItem(rect);
    }
    rects.clear();

    auto& labels = m_highlightLabels[pageIndex];
    for (auto* label : labels)
    {
        if (label && label->parentPlot())
            label->parentPlot()->removeItem(label);
    }
    labels.clear();

    // ---- 断开旧的 afterReplot 连接 ----
    {
        auto it = m_highlightReplotConns.find(pageIndex);
        if (it != m_highlightReplotConns.end())
        {
            disconnect(it.value());
            m_highlightReplotConns.erase(it);
        }
    }

    // ---- 计算新区间 ----
    auto& hlMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).highlightMgr;
    auto& dm = m_viewer.GetDataManager();
    auto intervals = hlMgr.computeIntervals(dm);

    // ---- 为每个区间创建 QCPItemRect + QCPItemText ----
    for (const auto& interval : intervals)
    {
        // 创建色块：宽度从 xStart 到 xEnd，高度充满可见 Y 轴
        auto* rect = new QCPItemRect(plot);
        double yUpper = plot->yAxis->range().upper;
        double yLower = plot->yAxis->range().lower;
        rect->topLeft->setCoords(interval.xStart, yUpper);
        rect->bottomRight->setCoords(interval.xEnd, yLower);
        rect->topLeft->setType(QCPItemPosition::ptPlotCoords);
        rect->bottomRight->setType(QCPItemPosition::ptPlotCoords);

        QColor fillColor = interval.color;
        fillColor.setAlpha(interval.alpha);
        rect->setPen(Qt::NoPen);
        rect->setBrush(fillColor);

        // 置于最底层（grid 之下）
        rect->setLayer("highlight");

        rects.push_back(rect);

        // 创建文字标注（在色块顶部居中，深灰色无背景）
        if (!interval.label.empty())
        {
            auto* textItem = new QCPItemText(plot);
            textItem->position->setCoords(
                (interval.xStart + interval.xEnd) / 2.0,
                yUpper
            );
            textItem->setText(QString::fromStdString(interval.label));
            // textItem->setFont(QFont("sans-serif", 9));  // 已注释：统一使用系统默认字体
            textItem->setColor(QColor(0x66, 0x66, 0x66));
            textItem->setPen(Qt::NoPen);
            textItem->setBrush(Qt::NoBrush);
            textItem->setPadding(QMargins(4, 2, 4, 2));
            textItem->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
            textItem->setSelectable(false);

            labels.push_back(textItem);
        }
    }

    // ---- 连接 rangeChanged：Y 轴范围变化后立即同步色块/标注（在 replot 之前触发，无延迟）----
    auto conn = connect(plot->yAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
        this, [this, pageIndex](const QCPRange& newRange)
        {
            if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
                return;

            double yUpper = newRange.upper;
            double yLower = newRange.lower;

            auto& r = m_highlightRects[pageIndex];
            for (auto* rect : r)
            {
                if (!rect) continue;
                rect->topLeft->setCoords(rect->topLeft->coords().x(), yUpper);
                rect->bottomRight->setCoords(rect->bottomRight->coords().x(), yLower);
            }

            auto& l = m_highlightLabels[pageIndex];
            for (auto* textItem : l)
            {
                if (!textItem) continue;
                textItem->position->setCoords(
                    textItem->position->coords().x(),
                    yUpper
                );
            }
        });

    m_highlightReplotConns[pageIndex] = conn;

    plot->replot();
}

