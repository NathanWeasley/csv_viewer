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
// 收藏夹
// ============================================================

void UI::loadBookmarkFile()
{
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
    QString path = QCoreApplication::applicationDirPath() + "/user/bookmarks.json";
    m_viewer.GetPlotManager().bookmarkMgr.loadFromFile(path.toStdString());

    m_bookmarkTree->clear();
    for (const auto& entry : m_viewer.GetPlotManager().bookmarkMgr.entries())
    {
        auto* item = new QTreeWidgetItem(m_bookmarkTree);
        item->setText(0, QString::fromStdString(entry.name));
    }
}

void UI::saveBookmarkFile()
{
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
    QString path = QCoreApplication::applicationDirPath() + "/user/bookmarks.json";
    m_viewer.GetPlotManager().bookmarkMgr.saveToFile(path.toStdString());
}

void UI::addBookmark(int pageIndex)
{
    auto& pm = m_viewer.GetPlotManager();
    const auto& info = pm.pageInfo(pageIndex);
    if (info.dataItems.empty()) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, QString::fromUtf8("加入收藏夹"),
        QString::fromUtf8("收藏名称："), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    std::string bmName = name.trimmed().toStdString();
    if (pm.bookmarkMgr.exists(bmName))
    {
        QMessageBox::warning(this, QString::fromUtf8("重名"),
            QString::fromUtf8("名称已存在，请使用其他名称。"));
        return;
    }

    auto* container = m_plotTabs->widget(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot) return;

    viewer::BookmarkEntry entry;
    entry.name = bmName;
    entry.xAxisColumn = info.xAxisColumn;
    entry.legendVisible = info.legendVisible;

    for (const auto& item : info.dataItems)
    {
        entry.dataItems.push_back(item);
        viewer::GraphStyleSnapshot gs;
        gs.dataItemName = item;

        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* g = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
            if (g && g->name().toStdString() == item)
            {
                QPen p = g->pen();
                gs.penStyle = static_cast<int>(p.style());
                gs.penWidth = p.width();
                gs.penColor = p.color().name().toStdString();
                QCPScatterStyle ss = g->scatterStyle();
                gs.scatterShape = static_cast<int>(ss.shape());
                gs.scatterSize = static_cast<int>(ss.size());
                gs.scatterColor = ss.pen().color().name().toStdString();
                break;
            }
        }

        auto* pe = pm.pageInfo(pageIndex).exprMgr.get(item);
        if (pe) { gs.expressionText = pe->expressionText; gs.isEdited = pe->isEdited; }
        entry.graphs.push_back(std::move(gs));
    }

    entry.highlights = info.highlightMgr.rules();
    if (!pm.bookmarkMgr.add(entry)) return;

    saveBookmarkFile();
    auto* bmItem = new QTreeWidgetItem(m_bookmarkTree);
    bmItem->setText(0, name);
}

void UI::onBookmarkDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    std::string name = item->text(0).toStdString();
    const auto* entry = m_viewer.GetPlotManager().bookmarkMgr.find(name);
    if (!entry) return;

    restoreBookmark(*entry);
}

void UI::restoreBookmark(const viewer::BookmarkEntry& entry)
{
    auto& pm = m_viewer.GetPlotManager();
    auto& dm = m_viewer.GetDataManager();
    int newIdx = pm.addPage(entry.name);
    pm.setXAxisColumn(newIdx, entry.xAxisColumn);

    for (const auto& it : entry.dataItems)
        pm.addDataItem(newIdx, it);

    auto* cw = m_plotTabs->widget(newIdx);
    auto* plot = cw ? cw->findChild<QCustomPlot*>() : nullptr;
    if (plot)
    {
        for (const auto& gs : entry.graphs)
        {
            viewer::QCPColumnGraph* graph = nullptr;
            for (int i = 0; i < plot->plottableCount(); ++i)
            {
                auto* g = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
                if (g && g->name().toStdString() == gs.dataItemName) { graph = g; break; }
            }
            if (!graph) continue;

            QPen pen(static_cast<Qt::PenStyle>(gs.penStyle));
            pen.setWidth(gs.penWidth);
            pen.setColor(QColor(QString::fromStdString(gs.penColor)));
            graph->setPen(pen);

            QCPScatterStyle ss;
            ss.setShape(static_cast<QCPScatterStyle::ScatterShape>(gs.scatterShape));
            ss.setSize(gs.scatterSize);
            ss.setPen(QPen(QColor(QString::fromStdString(gs.scatterColor))));
            graph->setScatterStyle(ss);

            if (gs.isEdited && !gs.expressionText.empty())
            {
                auto& exprMgr = pm.pageInfo(newIdx).exprMgr;
                exprMgr.setExpressionText(gs.dataItemName, gs.expressionText);
                exprMgr.recompute(gs.dataItemName, dm);
            }
        }

        auto& hlMgr = pm.pageInfo(newIdx).highlightMgr;
        hlMgr.clearAll();
        for (const auto& rule : entry.highlights) hlMgr.addRule(rule);
        renderHighlights(newIdx);

        if (entry.legendVisible) pm.setLegendVisible(newIdx, true);
        plot->rescaleAxes();
        plot->replot();
    }
}
