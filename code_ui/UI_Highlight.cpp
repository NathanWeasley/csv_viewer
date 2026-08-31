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
#include "code_viewer/jsonmgr/highlight_rule_json.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
QRectF highlightLabelRect(const QCPItemText* label)
{
    if (!label || !label->topLeft || !label->bottomRight)
        return {};
    return QRectF(label->topLeft->pixelPosition(),
                  label->bottomRight->pixelPosition()).normalized();
}

void arrangeHighlightLabels(QCustomPlot* plot, const QList<QCPItemText*>& labels)
{
    if (!plot || !plot->axisRect() || labels.isEmpty())
        return;

    struct Entry
    {
        QCPItemText* label = nullptr;
        qreal centerX = 0.0;
    };

    const qreal top = plot->axisRect()->rect().top();
    std::vector<Entry> ordered;
    ordered.reserve(static_cast<size_t>(labels.size()));

    // Every draw starts again at the top. This deliberately discards the
    // collision offset calculated by the previous draw.
    for (auto* label : labels)
    {
        if (!label || !label->visible())
            continue;
        QPointF pixel = label->position->pixelPosition();
        label->position->setPixelPosition(QPointF(pixel.x(), top));
        const QRectF rect = highlightLabelRect(label);
        ordered.push_back({label, rect.center().x()});
    }

    std::stable_sort(ordered.begin(), ordered.end(),
        [](const Entry& left, const Entry& right)
        {
            return left.centerX < right.centerX;
        });

    constexpr qreal kVerticalGap = 2.0;
    std::vector<QRectF> placed;
    placed.reserve(ordered.size());
    for (const Entry& entry : ordered)
    {
        QRectF candidate = highlightLabelRect(entry.label);
        for (;;)
        {
            qreal nextTop = candidate.top();
            for (const QRectF& previous : placed)
            {
                if (candidate.intersects(previous))
                    nextTop = std::max(nextTop, previous.bottom() + kVerticalGap);
            }

            const qreal delta = nextTop - candidate.top();
            if (delta <= 0.0)
                break;

            QPointF pixel = entry.label->position->pixelPosition();
            entry.label->position->setPixelPosition(
                QPointF(pixel.x(), pixel.y() + delta));
            candidate.translate(0.0, delta);
        }
        placed.push_back(candidate);
    }
}
}

std::vector<viewer::HighlightRule> UI::effectiveHighlightRules(int pageIndex) const
{
    std::vector<viewer::HighlightRule> result;
    if (pageIndex < 0 || pageIndex >= m_viewer.GetPlotManager().pageCount())
        return result;

    const auto& plotRules = m_viewer.GetPlotManager().pageInfo(pageIndex).highlightMgr.rules();
    return viewer::HighlightManager::mergeRules(m_globalHighlightMgr.rules(), plotRules);
}

bool UI::hasEffectiveHighlightRules(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_viewer.GetPlotManager().pageCount())
        return false;
    return m_globalHighlightMgr.ruleCount() > 0
        || m_viewer.GetPlotManager().pageInfo(pageIndex).highlightMgr.ruleCount() > 0;
}

void UI::renderAllHighlights()
{
    for (int pageIndex = 0; pageIndex < plotPageCount(); ++pageIndex)
        renderHighlights(pageIndex);
}

void UI::loadGlobalHighlightFile()
{
    const QString path = QCoreApplication::applicationDirPath() + "/user/highlight.json";
    std::vector<viewer::HighlightRule> rules;
    QString error;
    if (!viewer::HighlightRuleJson::loadFile(path.toStdString(), &rules, &error))
    {
        logOperationTrace(QString("global highlight load failed path=\"%1\" error=\"%2\"")
                          .arg(path, error));
        statusBar()->showMessage(
            QString::fromUtf8("全局高亮规则加载失败：%1").arg(error), 6000);
        return;
    }

    m_globalHighlightMgr.insertAllRules(std::move(rules));
    logOperationTrace(QString("global highlight loaded path=\"%1\" rules=%2")
                      .arg(path).arg(m_globalHighlightMgr.ruleCount()));
    renderAllHighlights();
}

bool UI::saveGlobalHighlightFile()
{
    const QString path = QCoreApplication::applicationDirPath() + "/user/highlight.json";
    QString error;
    if (!viewer::HighlightRuleJson::saveFile(
            path.toStdString(), m_globalHighlightMgr.rules(), &error))
    {
        logOperationTrace(QString("global highlight save failed path=\"%1\" error=\"%2\"")
                          .arg(path, error));
        QMessageBox::warning(
            this, QString::fromUtf8("全局高亮规则"),
            QString::fromUtf8("无法保存全局高亮规则：%1").arg(error));
        return false;
    }

    logOperationTrace(QString("global highlight saved path=\"%1\" rules=%2")
                      .arg(path).arg(m_globalHighlightMgr.ruleCount()));
    return true;
}

void UI::showGlobalHighlightDialog()
{
    const auto& columnNames = m_viewer.GetDataManager().GetColumnNames();
    std::vector<std::string> columns(columnNames.begin(), columnNames.end());

    HighlightDialog dialog(columns, this);
    dialog.setWindowTitle(QString::fromUtf8("全局高亮规则"));
    dialog.setRules(m_globalHighlightMgr.rules());
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_globalHighlightMgr.insertAllRules(dialog.getRules());
    saveGlobalHighlightFile();
    renderAllHighlights();
}

// ============================================================
// showHighlightDialog: 显示高亮规则配置对话框
// ============================================================

void UI::showHighlightDialog(int pageIndex)
{
    logOperationTrace(QString("highlight dialog enter page=%1 pages=%2")
                      .arg(pageIndex).arg(plotPageCount()));
    if (pageIndex < 0 || pageIndex >= plotPageCount())
    {
        logOperationTrace("highlight dialog aborted: invalid page");
        return;
    }

    auto& pm = m_viewer.GetPlotManager();
    auto& dm = m_viewer.GetDataManager();

    // 获取所有列名
    const auto& colNames = dm.GetColumnNames();
    std::vector<std::string> columns(colNames.begin(), colNames.end());

    // 创建对话框
    HighlightDialog dlg(columns, this);
    dlg.setWindowTitle(QString::fromUtf8("图窗高亮规则"));

    // 设置已有规则
    auto& hlMgr = pm.pageInfo(pageIndex).highlightMgr;
    dlg.setRules(hlMgr.rules());

    if (dlg.exec() != QDialog::Accepted)
    {
        logOperationTrace(QString("highlight dialog cancelled page=%1").arg(pageIndex));
        return;
    }

    // 写入规则
    auto newRules = dlg.getRules();
    hlMgr.clearAll();
    for (const auto& rule : newRules)
        hlMgr.addRule(rule);

    // 渲染高亮
    renderHighlights(pageIndex);
    logOperationTrace(QString("highlight dialog accepted page=%1 rules=%2")
                      .arg(pageIndex).arg(newRules.size()));
}

// ============================================================
// renderHighlights: 根据高亮规则绘制色块和文字标注
// ============================================================

void UI::renderHighlights(int pageIndex)
{
    logOperationTrace(QString("highlight render enter page=%1").arg(pageIndex));
    if (pageIndex < 0 || pageIndex >= plotPageCount())
        return;

    auto* container = getPlotContainer(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
        return;

    // 区间色块放在 grid 下方；时刻竖线放在 grid 上方、数据曲线下方。
    if (!plot->layer("highlight"))
        plot->addLayer("highlight", plot->layer("grid"), QCustomPlot::limBelow);
    if (!plot->layer("highlightLine"))
        plot->addLayer("highlightLine", plot->layer("grid"), QCustomPlot::limAbove);

    // ---- 清除旧的高亮元素 ----
    auto& rects = m_highlightRects[pageIndex];
    for (auto* rect : rects)
    {
        if (rect && rect->parentPlot())
            rect->parentPlot()->removeItem(rect);
    }
    rects.clear();

    auto& lines = m_highlightLines[pageIndex];
    for (auto* line : lines)
    {
        if (line && line->parentPlot())
            line->parentPlot()->removeItem(line);
    }
    lines.clear();

    auto& labels = m_highlightLabels[pageIndex];
    for (auto* label : labels)
    {
        if (label && label->parentPlot())
            label->parentPlot()->removeItem(label);
    }
    labels.clear();

    // ---- 断开旧的 afterLayout 连接 ----
    {
        auto it = m_highlightReplotConns.find(pageIndex);
        if (it != m_highlightReplotConns.end())
        {
            disconnect(it.value());
            m_highlightReplotConns.erase(it);
        }
    }

    // ---- 计算新区间 ----
    auto& pm = m_viewer.GetPlotManager();
    auto& dm = m_viewer.GetDataManager();
    const size_t xAxisColumn = pm.xAxisColumn(pageIndex);
    const auto& plotRules = pm.pageInfo(pageIndex).highlightMgr.rules();
    auto effectiveRules = effectiveHighlightRules(pageIndex);
    viewer::HighlightManager effectiveManager;
    effectiveManager.insertAllRules(std::move(effectiveRules));
    auto intervals = effectiveManager.computeIntervals(dm, xAxisColumn);
    logOperationTrace(QString("highlight intervals computed page=%1 globalRules=%2 plotRules=%3 effectiveRules=%4 intervals=%5 useIndex=%6 xColumn=%7")
                      .arg(pageIndex).arg(m_globalHighlightMgr.ruleCount()).arg(plotRules.size())
                      .arg(effectiveManager.ruleCount()).arg(intervals.size())
                      .arg(xAxisColumn == static_cast<size_t>(-1)).arg(xAxisColumn));

    // ---- 为每个命中结果创建区间色块或时刻竖线 ----
    for (const auto& interval : intervals)
    {
        double yUpper = plot->yAxis->range().upper;
        double yLower = plot->yAxis->range().lower;

        if (interval.presentation == viewer::HighlightPresentation::VerticalLine)
        {
            auto* line = new QCPItemLine(plot);
            line->start->setType(QCPItemPosition::ptPlotCoords);
            line->end->setType(QCPItemPosition::ptPlotCoords);
            line->start->setCoords(interval.xStart, yLower);
            line->end->setCoords(interval.xStart, yUpper);

            QColor lineColor = interval.color;
            lineColor.setAlpha(interval.alpha);
            QPen pen(lineColor);
            pen.setWidthF(2.0);
            pen.setCosmetic(true);
            line->setPen(pen);
            line->setSelectable(false);
            line->setLayer("highlightLine");
            lines.push_back(line);
        }
        else
        {
            // 创建色块：宽度从 xStart 到 xEnd，高度充满可见 Y 轴
            auto* rect = new QCPItemRect(plot);
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
        }

        // 创建文字标注（在色块顶部居中，深灰色无背景）
        if (!interval.label.empty())
        {
            auto* textItem = new QCPItemText(plot);
            textItem->position->setType(QCPItemPosition::ptPlotCoords);
            textItem->position->setAxes(plot->xAxis, plot->yAxis);
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

    // afterLayout runs for every on-screen/off-screen redraw and provides the
    // final axis rectangle for this frame. Rebuild vertical extents and label
    // collision offsets here so zooming, resizing and exporting share exactly
    // the same layout behavior.
    auto conn = connect(plot, &QCustomPlot::afterLayout,
        this, [this, pageIndex]()
        {
            if (pageIndex < 0 || pageIndex >= plotPageCount())
                return;

            QCustomPlot* currentPlot = getPlot(pageIndex);
            if (!currentPlot)
                return;

            const double yUpper = currentPlot->yAxis->range().upper;
            const double yLower = currentPlot->yAxis->range().lower;

            auto& r = m_highlightRects[pageIndex];
            for (auto* rect : r)
            {
                if (!rect) continue;
                rect->topLeft->setCoords(rect->topLeft->coords().x(), yUpper);
                rect->bottomRight->setCoords(rect->bottomRight->coords().x(), yLower);
            }

            auto& eventLines = m_highlightLines[pageIndex];
            for (auto* line : eventLines)
            {
                if (!line) continue;
                const double x = line->start->coords().x();
                line->start->setCoords(x, yLower);
                line->end->setCoords(x, yUpper);
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

            arrangeHighlightLabels(currentPlot, l);
        });

    m_highlightReplotConns[pageIndex] = conn;

    plot->replot();
    logOperationTrace(QString("highlight render leave page=%1 rects=%2 lines=%3 labels=%4")
                      .arg(pageIndex).arg(rects.size()).arg(lines.size()).arg(labels.size()));
}

