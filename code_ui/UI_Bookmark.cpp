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
#include <qcheckbox.h>
#include <qinputdialog.h>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QStyle>
#include <QApplication>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

namespace {
    constexpr int RoleType = Qt::UserRole + 1;
    constexpr int RolePath = Qt::UserRole + 2;
    constexpr int RoleName = Qt::UserRole + 3;
}

// ============================================================
// 收藏夹 - 树重建
// ============================================================

static void populateTree(QTreeWidgetItem* parentItem,
                          const viewer::BookmarkFolder& folder,
                          const std::string& parentPath)
{
    std::string curPath = parentPath.empty()
        ? folder.name
        : parentPath + "/" + folder.name;

    for (const auto& sub : folder.subFolders)
    {
        auto* subItem = new QTreeWidgetItem(parentItem);
        subItem->setText(0, QString::fromStdString(sub.name));
        subItem->setData(0, RoleType, QStringLiteral("folder"));
        std::string subPath = curPath.empty()
            ? sub.name
            : curPath + "/" + sub.name;
        subItem->setData(0, RolePath, QString::fromStdString(subPath));
        subItem->setData(0, RoleName, QString::fromStdString(sub.name));
        subItem->setIcon(0, qApp->style()->standardIcon(QStyle::SP_DirIcon));
        populateTree(subItem, sub, curPath);
    }

    for (const auto& e : folder.entries)
    {
        auto* eItem = new QTreeWidgetItem(parentItem);
        eItem->setText(0, QString::fromStdString(e.name));
        eItem->setData(0, RoleType, QStringLiteral("bookmark"));
        eItem->setData(0, RolePath, QString::fromStdString(curPath));
        eItem->setData(0, RoleName, QString::fromStdString(e.name));
        eItem->setIcon(0, qApp->style()->standardIcon(QStyle::SP_FileIcon));
    }
}

void UI::rebuildBookmarkTree()
{
    m_bookmarkTree->clear();
    const auto& root = m_viewer.GetPlotManager().bookmarkMgr.root();

    for (const auto& sub : root.subFolders)
    {
        auto* item = new QTreeWidgetItem(m_bookmarkTree);
        item->setText(0, QString::fromStdString(sub.name));
        item->setData(0, RoleType, QStringLiteral("folder"));
        item->setData(0, RolePath, QString::fromStdString(sub.name));
        item->setData(0, RoleName, QString::fromStdString(sub.name));
        item->setIcon(0, qApp->style()->standardIcon(QStyle::SP_DirIcon));
        populateTree(item, sub, std::string());
    }

    for (const auto& e : root.entries)
    {
        auto* item = new QTreeWidgetItem(m_bookmarkTree);
        item->setText(0, QString::fromStdString(e.name));
        item->setData(0, RoleType, QStringLiteral("bookmark"));
        item->setData(0, RolePath, QString());
        item->setData(0, RoleName, QString::fromStdString(e.name));
        item->setIcon(0, qApp->style()->standardIcon(QStyle::SP_FileIcon));
    }
}

// ============================================================
// 收藏夹 - 文件 I/O
// ============================================================

void UI::loadBookmarkFile()
{
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
    QString path = QCoreApplication::applicationDirPath() + "/user/bookmarks.json";
    m_viewer.GetPlotManager().bookmarkMgr.loadFromFile(path.toStdString());
    rebuildBookmarkTree();
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

    // 输入收藏名称和文件夹
    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("加入收藏夹"));
    auto* layout = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QString::fromUtf8("收藏名称（必填）"));
    layout->addRow(QString::fromUtf8("收藏名称："), nameEdit);

    auto* folderEdit = new QLineEdit(&dlg);
    folderEdit->setPlaceholderText(QString::fromUtf8("文件夹路径，如 folder/sub，留空则放在根目录"));
    layout->addRow(QString::fromUtf8("文件夹："), folderEdit);

    auto* btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(btnBox);

    if (dlg.exec() != QDialog::Accepted) return;

    std::string bmName = nameEdit->text().trimmed().toStdString();
    if (bmName.empty())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
            QString::fromUtf8("收藏名称不能为空。"));
        return;
    }

    std::string folderPath = folderEdit->text().trimmed().toStdString();

    // 如果指定了文件夹，检查文件夹是否存在
    if (!folderPath.empty())
    {
        if (!pm.bookmarkMgr.hasFolder(folderPath))
        {
            QMessageBox::warning(this, QString::fromUtf8("错误"),
                QString::fromUtf8("指定的文件夹不存在。"));
            return;
        }
    }

    // 全局唯一性检查
    if (!pm.bookmarkMgr.isNameUnique(bmName))
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
    entry.logX = (plot->xAxis->scaleType() == QCPAxis::stLogarithmic);
    entry.logY = (plot->yAxis->scaleType() == QCPAxis::stLogarithmic);

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
    if (!pm.bookmarkMgr.addBookmark(folderPath, entry)) return;

    saveBookmarkFile();
    rebuildBookmarkTree();
}

void UI::onBookmarkDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;

    QString type = item->data(0, RoleType).toString();
    if (type != QStringLiteral("bookmark"))
        return;   // 文件夹双击不处理（可自行展开/折叠）

    std::string folderPath = item->data(0, RolePath).toString().toStdString();
    std::string name       = item->data(0, RoleName).toString().toStdString();

    const auto* entry = m_viewer.GetPlotManager().bookmarkMgr.find(folderPath, name);
    if (!entry) return;

    restoreBookmark(*entry);
}

void UI::restoreBookmark(const viewer::BookmarkEntry& entry)
{
    auto& pm = m_viewer.GetPlotManager();
    auto& dm = m_viewer.GetDataManager();
    int newIdx = pm.addPage(entry.name);
    pm.setXAxisColumn(newIdx, entry.xAxisColumn);

    auto* cw = m_plotTabs->widget(newIdx);
    auto* plot = cw ? cw->findChild<QCustomPlot*>() : nullptr;

    // 抑制中间重绘，全部设置完成后统一 replot
    if (plot) plot->setUpdatesEnabled(false);

    for (const auto& it : entry.dataItems)
        pm.addDataItem(newIdx, it);

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
                if (exprMgr.recompute(gs.dataItemName, dm))
                {
                    // 重新绑定 graph 到表达式计算结果列（recompute 后 computedData 持有新数据）
                    viewer::PlotExpression* pe = exprMgr.get(gs.dataItemName);
                    if (graph && pe && pe->computedData)
                    {
                        size_t xIdx = pm.xAxisColumn(newIdx);
                        const viewer::Column* xCol;
                        if (xIdx != static_cast<size_t>(-1))
                            xCol = dm.GetColumn(xIdx);
                        else
                        {
                            dm.ensureIndexColumnBuilt();
                            xCol = dm.GetIndexColumn();
                        }
                        graph->setDataColumns(xCol, pe->computedData.get());
                        graph->notifyDataChanged();
                    }
                }
            }
        }

        // 更新工具栏 ComboList 星号显示（表达式编辑后的名称标记）
        {
            auto* vbox = cw->findChild<QVBoxLayout*>();
            if (vbox && vbox->count() >= 1)
            {
                auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
                if (toolbar)
                {
                    auto* hb = toolbar->findChild<QHBoxLayout*>();
                    if (hb && hb->count() >= 11)
                    {
                        auto* cmbDataItem = qobject_cast<QComboBox*>(hb->itemAt(0)->widget());
                        if (cmbDataItem)
                        {
                            auto& exprMgr = pm.pageInfo(newIdx).exprMgr;
                            cmbDataItem->blockSignals(true);
                            for (int i = 0; i < cmbDataItem->count(); ++i)
                            {
                                std::string name = cmbDataItem->itemData(i, Qt::UserRole).toString().toStdString();
                                viewer::PlotExpression* pe = exprMgr.get(name);
                                if (pe && pe->isEdited)
                                    cmbDataItem->setItemText(i, QString::fromStdString(name + "*"));
                            }
                            cmbDataItem->blockSignals(false);
                        }
                    }
                }
            }
        }

        // 更新表达式编辑栏（显示已编辑的表达式文本）
        auto* lineEdit = m_exprLineEdits.value(newIdx, nullptr);
        if (lineEdit)
        {
            auto& exprMgr = pm.pageInfo(newIdx).exprMgr;
            // 取第一个有表达式的数据项来更新表达式栏
            for (const auto& gs : entry.graphs)
            {
                viewer::PlotExpression* pe = exprMgr.get(gs.dataItemName);
                if (pe && pe->isEdited)
                {
                    lineEdit->blockSignals(true);
                    lineEdit->setText(QString::fromStdString(pe->expressionText));
                    lineEdit->blockSignals(false);
                    break;
                }
            }
        }

        auto& hlMgr = pm.pageInfo(newIdx).highlightMgr;
        hlMgr.clearAll();
        for (const auto& rule : entry.highlights) hlMgr.addRule(rule);
        renderHighlights(newIdx);

        if (entry.legendVisible) pm.setLegendVisible(newIdx, true);

        // 恢复对数轴状态
        plot->xAxis->setScaleType(entry.logX ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
        plot->yAxis->setScaleType(entry.logY ? QCPAxis::stLogarithmic : QCPAxis::stLinear);

        // 同步工具栏复选框
        auto* vbox = cw->findChild<QVBoxLayout*>();
        if (vbox && vbox->count() >= 1)
        {
            auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
            if (toolbar)
            {
                auto* hb = toolbar->findChild<QHBoxLayout*>();
                if (hb && hb->count() >= 10)
                {
                    auto* chkLogX = qobject_cast<QCheckBox*>(hb->itemAt(7)->widget());
                    auto* chkLogY = qobject_cast<QCheckBox*>(hb->itemAt(8)->widget());
                    if (chkLogX) chkLogX->blockSignals(true);
                    if (chkLogX) chkLogX->setChecked(entry.logX);
                    if (chkLogX) chkLogX->blockSignals(false);
                    if (chkLogY) chkLogY->blockSignals(true);
                    if (chkLogY) chkLogY->setChecked(entry.logY);
                    if (chkLogY) chkLogY->blockSignals(false);
                }
            }
        }

        // 恢复重绘并统一执行一次
        plot->setUpdatesEnabled(true);
        plot->rescaleAxes();
        plot->replot();
    }
}

// ============================================================
// 收藏夹 - 右键菜单
// ============================================================

void UI::onBookmarkTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_bookmarkTree->itemAt(pos);
    QMenu menu;

    if (!item)
    {
        QAction* newFolderAction = menu.addAction(QString::fromUtf8("新建文件夹"));
        connect(newFolderAction, &QAction::triggered, this, [this]()
        {
            bool ok = false;
            QString name = QInputDialog::getText(this,
                QString::fromUtf8("新建文件夹"),
                QString::fromUtf8("文件夹名称："),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) return;

            std::string folderName = name.trimmed().toStdString();
            if (!m_viewer.GetPlotManager().bookmarkMgr.addFolder("", folderName))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法创建文件夹（名称可能已存在）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
        menu.exec(m_bookmarkTree->viewport()->mapToGlobal(pos));
        return;
    }

    QString type = item->data(0, RoleType).toString();
    std::string curPath = item->data(0, RolePath).toString().toStdString();
    std::string curName = item->data(0, RoleName).toString().toStdString();

    if (type == QStringLiteral("folder"))
    {
        QAction* newSubAction = menu.addAction(QString::fromUtf8("新建子文件夹"));
        connect(newSubAction, &QAction::triggered, this, [this, curPath]()
        {
            bool ok = false;
            QString name = QInputDialog::getText(this,
                QString::fromUtf8("新建子文件夹"),
                QString::fromUtf8("子文件夹名称："),
                QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) return;

            std::string subName = name.trimmed().toStdString();
            if (!m_viewer.GetPlotManager().bookmarkMgr.addFolder(curPath, subName))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法创建子文件夹（名称可能已存在）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
        menu.addSeparator();

        QAction* renameAction = menu.addAction(QString::fromUtf8("重命名"));
        connect(renameAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            bool ok = false;
            QString newName = QInputDialog::getText(this,
                QString::fromUtf8("重命名文件夹"),
                QString::fromUtf8("新名称："),
                QLineEdit::Normal,
                QString::fromStdString(curName), &ok);
            if (!ok || newName.trimmed().isEmpty()) return;
            if (newName.trimmed().toStdString() == curName) return;

            if (!m_viewer.GetPlotManager().bookmarkMgr.renameFolder(
                    curPath, newName.trimmed().toStdString()))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法重命名文件夹（名称可能已存在）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });

        QAction* moveAction = menu.addAction(QString::fromUtf8("移动到..."));
        connect(moveAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            bool ok = false;
            QString target = QInputDialog::getText(this,
                QString::fromUtf8("移动文件夹"),
                QString::fromUtf8("目标父文件夹路径（留空=根目录）："),
                QLineEdit::Normal, QString(), &ok);
            if (!ok) return;

            std::string targetPath = target.trimmed().toStdString();
            if (targetPath == curPath) return;

            if (!targetPath.empty() &&
                !m_viewer.GetPlotManager().bookmarkMgr.hasFolder(targetPath))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("目标文件夹不存在。"));
                return;
            }

            if (!m_viewer.GetPlotManager().bookmarkMgr.moveFolder(curPath, targetPath))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法移动文件夹（名称冲突或非法操作）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
        menu.addSeparator();

        QAction* deleteAction = menu.addAction(QString::fromUtf8("删除"));
        connect(deleteAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            auto result = QMessageBox::question(this,
                QString::fromUtf8("确认删除"),
                QString::fromUtf8("确定要删除文件夹 \"%1\" 及其所有内容吗？")
                    .arg(QString::fromStdString(curName)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (result != QMessageBox::Yes) return;

            m_viewer.GetPlotManager().bookmarkMgr.removeFolder(curPath);
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
    }
    else if (type == QStringLiteral("bookmark"))
    {
        QAction* renameAction = menu.addAction(QString::fromUtf8("重命名"));
        connect(renameAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            bool ok = false;
            QString newName = QInputDialog::getText(this,
                QString::fromUtf8("重命名收藏项"),
                QString::fromUtf8("新名称："),
                QLineEdit::Normal,
                QString::fromStdString(curName), &ok);
            if (!ok || newName.trimmed().isEmpty()) return;
            if (newName.trimmed().toStdString() == curName) return;

            if (!m_viewer.GetPlotManager().bookmarkMgr.renameBookmark(
                    curPath, curName, newName.trimmed().toStdString()))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法重命名收藏项（名称可能已存在）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });

        QAction* moveAction = menu.addAction(QString::fromUtf8("移动到..."));
        connect(moveAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            bool ok = false;
            QString target = QInputDialog::getText(this,
                QString::fromUtf8("移动收藏项"),
                QString::fromUtf8("目标文件夹路径（留空=根目录）："),
                QLineEdit::Normal, QString(), &ok);
            if (!ok) return;

            std::string targetPath = target.trimmed().toStdString();
            if (targetPath == curPath) return;

            if (!targetPath.empty() &&
                !m_viewer.GetPlotManager().bookmarkMgr.hasFolder(targetPath))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("目标文件夹不存在。"));
                return;
            }

            if (!m_viewer.GetPlotManager().bookmarkMgr.moveBookmark(
                    curPath, targetPath, curName))
            {
                QMessageBox::warning(this,
                    QString::fromUtf8("错误"),
                    QString::fromUtf8("无法移动收藏项（名称冲突）。"));
                return;
            }
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
        menu.addSeparator();

        QAction* deleteAction = menu.addAction(QString::fromUtf8("删除"));
        connect(deleteAction, &QAction::triggered, this, [this, curPath, curName]()
        {
            auto result = QMessageBox::question(this,
                QString::fromUtf8("确认删除"),
                QString::fromUtf8("确定要删除收藏项 \"%1\" 吗？")
                    .arg(QString::fromStdString(curName)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (result != QMessageBox::Yes) return;

            m_viewer.GetPlotManager().bookmarkMgr.removeBookmark(curPath, curName);
            saveBookmarkFile();
            rebuildBookmarkTree();
        });
    }

    menu.exec(m_bookmarkTree->viewport()->mapToGlobal(pos));
}
