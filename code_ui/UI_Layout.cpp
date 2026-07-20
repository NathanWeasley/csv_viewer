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
#include <qabstractitemview.h>
#include <qdialog.h>
#include <qdialogbuttonbox.h>
#include <qlistwidget.h>
#include <qmenu.h>
#include <qtabbar.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qinputdialog.h>
#include <qtimer.h>
#include <qwindow.h>
#include <qapplication.h>
#include <qstyle.h>
#include <utility>
#include <cmath>
#include <algorithm>

#include "icons_base64.h"
#include "DockAreaWidget.h"
#include "DockAreaTitleBar.h"
#include "DockSplitter.h"
#include "FloatingDockContainer.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "code_viewer/base/trace_logger.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include "XAxisDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

extern bool isSystemInDark();

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

static QString xAxisMonotonicWarning(const viewer::DataManager& dm)
{
    const size_t xIdx = dm.GetXAxisColumn();
    const auto& colNames = dm.GetColumnNames();
    const size_t rowCount = dm.GetRowCount();

    if (xIdx == static_cast<size_t>(-1) || xIdx >= colNames.size() || rowCount < 2)
        return QString();

    double previous = dm.GetValueAsDouble(xIdx, 0);
    if (!std::isfinite(previous))
    {
        return QString("自动选择的X轴数据 \"%1\" 的第1行含有非数值项。")
            .arg(QString::fromStdString(colNames[xIdx]));
    }

    for (size_t row = 1; row < rowCount; ++row)
    {
        const double value = dm.GetValueAsDouble(xIdx, row);
        if (!std::isfinite(value))
        {
            return QString("自动选择的X轴数据 \"%1\" 在第 %2 行含有非数值项。")
                .arg(QString::fromStdString(colNames[xIdx]))
                .arg(row + 1);
        }
        if (value < previous)
        {
            return QString("自动选择的X轴数据 \"%1\" 在第 %2 行处不是单调递增的。")
                .arg(QString::fromStdString(colNames[xIdx]))
                .arg(row + 1);
        }
        previous = value;
    }

    return QString();
}

static bool isWidgetDescendantOf(const QWidget* child, const QWidget* ancestor)
{
    for (const QWidget* current = child; current; current = current->parentWidget())
    {
        if (current == ancestor)
            return true;
    }
    return false;
}

static ads::CDockSplitter* findCommonVerticalPlotSplitter(
    const QList<ads::CDockAreaWidget*>& rowAreas,
    int expectedRowCount)
{
    if (rowAreas.isEmpty() || expectedRowCount <= 0)
        return nullptr;

    for (QWidget* current = rowAreas.first(); current; current = current->parentWidget())
    {
        auto* splitter = qobject_cast<ads::CDockSplitter*>(current);
        if (!splitter || splitter->orientation() != Qt::Vertical || splitter->count() != expectedRowCount)
            continue;

        bool containsAllRows = true;
        for (auto* area : rowAreas)
        {
            if (!area || !isWidgetDescendantOf(area, splitter))
            {
                containsAllRows = false;
                break;
            }
        }

        if (containsAllRows)
            return splitter;
    }

    return nullptr;
}

namespace
{
constexpr int kDataTreeKindRole = Qt::UserRole;
constexpr int kDataTreeNameRole = Qt::UserRole + 1;
constexpr int kDataTreeGroupKind = 1;
constexpr int kDataTreeLeafKind = 2;
constexpr int kAutoGroupMinItemCount = 4;

QString dataGroupPrefix(const QString& name)
{
    const int underscorePos = name.indexOf('_');
    if (underscorePos <= 0)
        return QString();
    return name.left(underscorePos);
}
}

#ifdef Q_OS_WIN
static void detachFloatingDockNativeOwner(ads::CFloatingDockContainer* floating)
{
    if (!floating)
        return;

    floating->winId();
    HWND hwnd = reinterpret_cast<HWND>(floating->windowHandle() ? floating->windowHandle()->winId()
                                                                : floating->winId());
    if (!hwnd)
        return;

    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOPMOST)
    {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    else
    {
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}
#endif

void UI::createMain()
{
    logOperationTrace("create main UI enter");
    ///< Center plot area — 内层 QADS DockManager 管理多个图窗页面
    // QADS 全局标志已在 UI::UI() 构造函数中设置（在任何 CDockManager 创建之前）
    m_plotDockManager = new ads::CDockManager();

#ifdef Q_OS_WIN
    connect(m_plotDockManager, &ads::CDockManager::floatingWidgetCreated, this,
        [this](ads::CFloatingDockContainer* floating)
        {
            Q_UNUSED(this);
            QTimer::singleShot(0, floating, [floating]()
            {
                detachFloatingDockNativeOwner(floating);
            });
        });
#endif

    // 连接内层 dock widget 聚焦信号到 PlotManager 激活
    connectInnerDockSignals();

    m_plotDock = new ads::CDockWidget("Plot");
    m_plotDock->setWidget(m_plotDockManager);
    m_plotDock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_plotDock);

    ///< Left DateTree
    m_dataTree = new QTreeWidget();
    m_dataTree->setHeaderHidden(true);
    m_dataTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dataTree, &QTreeWidget::customContextMenuRequested,
            this, &UI::onDataTreeContextMenu);
    connect(m_dataTree, &QTreeWidget::itemDoubleClicked, this, &UI::onDataItemDoubleClicked);

    m_dataDock = new ads::CDockWidget("Data");
    m_dataDock->setWidget(m_dataTree);
    m_dataDock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_dataDock, m_plotDock->dockAreaWidget());

    ///< Right Bookmark Tree
    m_bookmarkTree = new QTreeWidget();
    m_bookmarkTree->setHeaderHidden(true);
    m_bookmarkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkDock = new ads::CDockWidget("Bookmarks");
    m_bookmarkDock->setWidget(m_bookmarkTree);
    m_bookmarkDock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_bookmarkDock, m_plotDock->dockAreaWidget());
    hideFixedDockTitleBars();

    connect(m_dockManager, &ads::CDockManager::dockAreaCreated, this,
        [this](ads::CDockAreaWidget* /*area*/)
        {
            hideFixedDockTitleBars();
            QTimer::singleShot(0, this, [this]()
            {
                hideFixedDockTitleBars();
            });
        });

    connect(m_bookmarkTree, &QTreeWidget::itemDoubleClicked, this, &UI::onBookmarkDoubleClicked);
    connect(m_bookmarkTree, &QTreeWidget::customContextMenuRequested,
            this, &UI::onBookmarkTreeContextMenu);

    ///< Bind PlotManager callbacks
    bindPlotManagerCallbacks();
    logOperationTrace(QString("create main UI leave dockManager=0x%1 plotDockManager=0x%2 dataTree=0x%3 bookmarkTree=0x%4")
                      .arg(reinterpret_cast<quintptr>(m_dockManager), 0, 16)
                      .arg(reinterpret_cast<quintptr>(m_plotDockManager), 0, 16)
                      .arg(reinterpret_cast<quintptr>(m_dataTree), 0, 16)
                      .arg(reinterpret_cast<quintptr>(m_bookmarkTree), 0, 16));
}

void UI::createStatusbar()
{
    // X-axis label (left side)
    m_xAxisLabel = new QLabel("X: (none)");
    statusBar()->addWidget(m_xAxisLabel);

    // Cursor status label (between X-axis and progress bar)
    m_cursorStatusLabel = new QLabel("");
    {
        bool dark = isSystemInDark();
        QString color = dark ? "#FFD700" : "#2266cc";
        m_cursorStatusLabel->setStyleSheet(
            QString("color: %1; font-weight: bold; padding: 0 12px;").arg(color));
    }
    statusBar()->addWidget(m_cursorStatusLabel);

    // 隐藏状态栏控件之间的竖线分隔符
    statusBar()->setStyleSheet("QStatusBar::item { border: none; }");

    // Progress bar (right side)
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setMaximumWidth(300);
    m_progressBar->hide();
    statusBar()->addPermanentWidget(m_progressBar);

    // Connect Viewer signals to progress bar
    connect(&m_viewer, &viewer::Viewer::LoadStarted, this, [this](int totalFiles)
    {
        logFileTrace(QString("load started files=%1 autoGrouping=%2 forceGrouping=%3")
                     .arg(totalFiles).arg(m_autoGroupingEnabled).arg(m_forceDataGrouping));
        m_pendingSkippedFiles.clear();
        m_progressBar->setValue(0);
        m_progressBar->show();
    });

    connect(&m_viewer, &viewer::Viewer::BusyProgressChanged, this, [this](float globalProgress)
    {
        m_progressBar->setValue(static_cast<int>(globalProgress * 1000.0f));
    });

    connect(&m_viewer, &viewer::Viewer::LoadSkippedFiles, this, [this](const QStringList& files)
    {
        logFileTrace(QString("load skipped files count=%1 first=\"%2\"")
                     .arg(files.size()).arg(files.isEmpty() ? QString() : files.first()));
        m_pendingSkippedFiles = files;
    });

    connect(&m_viewer, &viewer::Viewer::LoadFinished, this, [this]()
    {
        logFileTrace(QString("load finished signal columns=%1 rows=%2 skipped=%3")
                     .arg(m_viewer.GetDataManager().GetColumnCount())
                     .arg(m_viewer.GetDataManager().GetRowCount())
                     .arg(m_pendingSkippedFiles.size()));
        m_progressBar->setValue(1000);
        m_progressBar->hide();

        rebuildDataTree();
        const auto& colNames = m_viewer.GetDataManager().GetColumnNames();

        // Update X-axis label
        if (m_viewer.GetDataManager().GetColumnCount() > 0)
        {
            size_t xIdx = m_viewer.GetDataManager().GetXAxisColumn();
            if (xIdx < colNames.size())
                m_xAxisLabel->setText(QString("X: %1").arg(QString::fromStdString(colNames[xIdx])));
            else
                m_xAxisLabel->setText("X: (none)");
        }
        else
        {
            m_xAxisLabel->setText("X: (none)");
        }

        if (!m_pendingSkippedFiles.isEmpty())
        {
            QMessageBox::information(this,
                                     QString::fromUtf8("未载入的CSV文件"),
                                     QString::fromUtf8("下列CSV文件被跳过，因为列数、列名或数据类型与先前载入的CSV不一致：\n\n%1")
                                         .arg(m_pendingSkippedFiles.join("\n")));
            m_pendingSkippedFiles.clear();
        }

        const QString monotonicWarning = xAxisMonotonicWarning(m_viewer.GetDataManager());
        if (!monotonicWarning.isEmpty())
            logXAxisTrace(QString("loaded X-axis monotonic warning: %1").arg(monotonicWarning));
        if (!monotonicWarning.isEmpty())
            QMessageBox::warning(this, QString::fromUtf8("X轴数据告警"), monotonicWarning);
    });

    // Handle cross-file column validation errors
    connect(&m_viewer, &viewer::Viewer::LoadError, this, [this](const QString& message)
    {
        logFileTrace(QString("load error: %1").arg(message));
        m_progressBar->setValue(0);
        m_progressBar->hide();
        m_dataTree->clear();
        m_xAxisLabel->setText("X: (none)");
        QMessageBox::critical(this, "CSV载入错误", message);
    });
}

bool UI::isDataGroupingEnabledForDisplay() const
{
    return m_forceDataGrouping || m_autoGroupingEnabled;
}

void UI::rebuildDataTree()
{
    if (!m_dataTree)
        return;

    m_dataTree->clear();

    const auto& colNames = m_viewer.GetDataManager().GetColumnNames();
    if (colNames.empty())
        return;

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    if (!isDataGroupingEnabledForDisplay())
    {
        for (const auto& name : colNames)
        {
            auto* item = new QTreeWidgetItem(m_dataTree);
            const QString qName = QString::fromStdString(name);
            item->setText(0, qName);
            item->setIcon(0, fileIcon);
            item->setData(0, kDataTreeKindRole, kDataTreeLeafKind);
            item->setData(0, kDataTreeNameRole, qName);
        }
        return;
    }

    QHash<QString, int> prefixCounts;
    for (const auto& name : colNames)
    {
        const QString prefix = dataGroupPrefix(QString::fromStdString(name));
        if (!prefix.isEmpty())
            ++prefixCounts[prefix];
    }

    QHash<QString, QTreeWidgetItem*> groupItems;
    for (const auto& name : colNames)
    {
        const QString qName = QString::fromStdString(name);
        const QString prefix = dataGroupPrefix(qName);
        const bool shouldGroup = !prefix.isEmpty() && prefixCounts.value(prefix) >= kAutoGroupMinItemCount;

        if (shouldGroup)
        {
            auto* groupItem = groupItems.value(prefix, nullptr);
            if (!groupItem)
            {
                groupItem = new QTreeWidgetItem(m_dataTree);
                groupItem->setText(0, prefix);
                groupItem->setIcon(0, folderIcon);
                groupItem->setData(0, kDataTreeKindRole, kDataTreeGroupKind);
                groupItems.insert(prefix, groupItem);
            }

            auto* childItem = new QTreeWidgetItem(groupItem);
            childItem->setText(0, qName);
            childItem->setIcon(0, fileIcon);
            childItem->setData(0, kDataTreeKindRole, kDataTreeLeafKind);
            childItem->setData(0, kDataTreeNameRole, qName);
        }
        else
        {
            auto* item = new QTreeWidgetItem(m_dataTree);
            item->setText(0, qName);
            item->setIcon(0, fileIcon);
            item->setData(0, kDataTreeKindRole, kDataTreeLeafKind);
            item->setData(0, kDataTreeNameRole, qName);
        }
    }

    m_dataTree->expandAll();
}

void UI::onDataTreeContextMenu(const QPoint& pos)
{
    if (!m_dataTree)
        return;

    auto* item = m_dataTree->itemAt(pos);
    if (!item)
        return;

    m_dataTree->setCurrentItem(item);

    const int itemKind = item->data(0, kDataTreeKindRole).toInt();
    QMenu menu;

    if (itemKind == kDataTreeGroupKind)
    {
        auto* plotAllAction = menu.addAction(QString::fromUtf8("全部绘图"));
        connect(plotAllAction, &QAction::triggered, this, [this, item]()
        {
            plotDataGroupInNewPage(item);
        });
    }
    else
    {
        const QString dataName = item->data(0, kDataTreeNameRole).toString();
        if (dataName.isEmpty())
            return;

        auto* copyAction = menu.addAction(QString::fromUtf8("复制数据名"));
        connect(copyAction, &QAction::triggered, this, [dataName]()
        {
            QApplication::clipboard()->setText(dataName);
        });
    }

    if (!menu.isEmpty())
        menu.exec(m_dataTree->viewport()->mapToGlobal(pos));
}

void UI::plotDataGroupInNewPage(QTreeWidgetItem* groupItem)
{
    if (!groupItem || groupItem->childCount() <= 0)
        return;

    auto& pm = m_viewer.GetPlotManager();
    const int pageIndex = pm.addPage(groupItem->text(0).toStdString());
    pm.setActivePage(pageIndex);

    for (int i = 0; i < groupItem->childCount(); ++i)
    {
        auto* childItem = groupItem->child(i);
        if (!childItem)
            continue;

        const QString dataName = childItem->data(0, kDataTreeNameRole).toString();
        if (!dataName.isEmpty())
            plotDataColumnByName(dataName);
    }
}

QString UI::plotPageDisplayName(int pageIndex) const
{
    const auto& pm = m_viewer.GetPlotManager();
    if (pageIndex < 0 || pageIndex >= pm.pageCount())
        return QString();

    const QString title = QString::fromStdString(pm.pageInfo(pageIndex).title);
    return QStringLiteral("[%1] %2").arg(pageIndex + 1).arg(title);
}

bool UI::isLinkEligiblePlotPage(int pageIndex) const
{
    const auto& pm = m_viewer.GetPlotManager();
    if (pageIndex < 0 || pageIndex >= pm.pageCount())
        return false;

    const auto& pageInfo = pm.pageInfo(pageIndex);
    if (!pageInfo.isFFT)
        return true;

    return pageInfo.title.rfind("STFT:", 0) == 0;
}

int UI::linkedXAxisGroupIndexForPage(int pageIndex) const
{
    for (int groupIndex = 0; groupIndex < m_linkedXAxisGroups.size(); ++groupIndex)
    {
        if (m_linkedXAxisGroups[groupIndex].contains(pageIndex))
            return groupIndex;
    }
    return -1;
}

void UI::cleanupLinkedXAxisGroups()
{
    const auto& pm = m_viewer.GetPlotManager();
    Q_UNUSED(pm);
    const int groupsBefore = m_linkedXAxisGroups.size();

    QList<QList<int>> cleanedGroups;
    QSet<int> usedPages;

    for (const auto& group : m_linkedXAxisGroups)
    {
        QList<int> cleanedMembers;
        for (int pageIndex : group)
        {
            if (!isLinkEligiblePlotPage(pageIndex))
                continue;
            if (usedPages.contains(pageIndex))
                continue;

            usedPages.insert(pageIndex);
            cleanedMembers.append(pageIndex);
        }

        std::sort(cleanedMembers.begin(), cleanedMembers.end());
        if (cleanedMembers.size() >= 2)
            cleanedGroups.append(cleanedMembers);
    }

    m_linkedXAxisGroups = cleanedGroups;
    logXAxisTrace(QString("cleanup link groups groupsBefore=%1 groupsAfter=%2 usedPages=%3")
                  .arg(groupsBefore).arg(m_linkedXAxisGroups.size()).arg(usedPages.size()));
}

void UI::reindexLinkedXAxisGroupsAfterRemoval(int removedPageIndex)
{
    if (removedPageIndex < 0)
        return;

    logXAxisTrace(QString("reindex link groups enter removedPage=%1 groups=%2")
                  .arg(removedPageIndex).arg(m_linkedXAxisGroups.size()));

    for (auto& group : m_linkedXAxisGroups)
    {
        QList<int> shifted;
        for (int pageIndex : group)
        {
            if (pageIndex == removedPageIndex)
                continue;
            shifted.append(pageIndex > removedPageIndex ? pageIndex - 1 : pageIndex);
        }
        group = shifted;
    }

    cleanupLinkedXAxisGroups();
    logXAxisTrace(QString("reindex link groups leave removedPage=%1 groups=%2")
                  .arg(removedPageIndex).arg(m_linkedXAxisGroups.size()));
}

void UI::syncLinkedXAxisRange(int sourcePageIndex, const QCPRange& newRange)
{
    if (m_syncingLinkedXAxis)
        return;

    const int groupIndex = linkedXAxisGroupIndexForPage(sourcePageIndex);
    if (groupIndex < 0 || groupIndex >= m_linkedXAxisGroups.size())
        return;

    m_syncingLinkedXAxis = true;

    const auto& members = m_linkedXAxisGroups[groupIndex];
    for (int pageIndex : members)
    {
        if (pageIndex == sourcePageIndex)
            continue;

        auto* plot = getPlot(pageIndex);
        if (!plot)
            continue;

        if (plot->xAxis->range() == newRange)
            continue;

        plot->xAxis->setRange(newRange);
        plot->replot(QCustomPlot::rpQueuedRefresh);
    }

    m_syncingLinkedXAxis = false;
}

void UI::showLinkXAxisDialog()
{
    auto& pm = m_viewer.GetPlotManager();
    logXAxisTrace(QString("link dialog enter pages=%1 groups=%2 syncing=%3")
                  .arg(pm.pageCount()).arg(m_linkedXAxisGroups.size()).arg(m_syncingLinkedXAxis));

    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("X轴联动设置"));
    dlg.resize(760, 440);

    auto workingGroups = m_linkedXAxisGroups;

    auto* mainLayout = new QVBoxLayout(&dlg);
    auto* bodyLayout = new QHBoxLayout();
    mainLayout->addLayout(bodyLayout, 1);

    auto* leftLayout = new QVBoxLayout();
    auto* leftLabel = new QLabel(QString::fromUtf8("可选图窗"));
    auto* listAvailable = new QListWidget();
    listAvailable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftLayout->addWidget(leftLabel);
    leftLayout->addWidget(listAvailable, 1);

    auto* midLayout = new QVBoxLayout();
    midLayout->addStretch();
    auto* btnLink = new QPushButton(QString::fromUtf8("关联 ->"));
    auto* btnRemove = new QPushButton(QString::fromUtf8("<- 移除"));
    midLayout->addWidget(btnLink);
    midLayout->addWidget(btnRemove);
    midLayout->addStretch();

    auto* rightLayout = new QVBoxLayout();
    auto* rightLabel = new QLabel(QString::fromUtf8("联动分组"));
    auto* treeLinked = new QTreeWidget();
    treeLinked->setHeaderHidden(true);
    treeLinked->setSelectionMode(QAbstractItemView::SingleSelection);
    rightLayout->addWidget(rightLabel);
    rightLayout->addWidget(treeLinked, 1);

    bodyLayout->addLayout(leftLayout, 1);
    bodyLayout->addLayout(midLayout);
    bodyLayout->addLayout(rightLayout, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    auto rebuildWidgets = [&, this]()
    {
        listAvailable->clear();
        treeLinked->clear();

        QSet<int> groupedPages;
        for (const auto& group : workingGroups)
        {
            for (int pageIndex : group)
                groupedPages.insert(pageIndex);
        }

        for (int pageIndex = 0; pageIndex < pm.pageCount(); ++pageIndex)
        {
            if (!isLinkEligiblePlotPage(pageIndex))
                continue;

            auto* item = new QListWidgetItem(plotPageDisplayName(pageIndex), listAvailable);
            item->setData(Qt::UserRole, pageIndex);
            if (groupedPages.contains(pageIndex))
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        }

        for (int groupIndex = 0; groupIndex < workingGroups.size(); ++groupIndex)
        {
            auto* groupItem = new QTreeWidgetItem(treeLinked);
            groupItem->setText(0, QStringLiteral("GROUP %1").arg(groupIndex + 1));
            groupItem->setData(0, Qt::UserRole, -1);
            groupItem->setData(0, Qt::UserRole + 1, groupIndex);
            groupItem->setExpanded(true);

            for (int pageIndex : workingGroups[groupIndex])
            {
                auto* childItem = new QTreeWidgetItem(groupItem);
                childItem->setText(0, plotPageDisplayName(pageIndex));
                childItem->setData(0, Qt::UserRole, pageIndex);
                childItem->setData(0, Qt::UserRole + 1, groupIndex);
            }
        }

        btnLink->setEnabled(listAvailable->selectedItems().size() >= 2);
        btnRemove->setEnabled(treeLinked->currentItem() != nullptr);
    };

    connect(listAvailable, &QListWidget::itemSelectionChanged, &dlg, [=]()
    {
        btnLink->setEnabled(listAvailable->selectedItems().size() >= 2);
    });

    connect(treeLinked, &QTreeWidget::itemSelectionChanged, &dlg, [=]()
    {
        btnRemove->setEnabled(treeLinked->currentItem() != nullptr);
    });

    connect(btnLink, &QPushButton::clicked, &dlg, [&, this]()
    {
        QList<int> selectedPages;
        for (auto* item : listAvailable->selectedItems())
        {
            if (!item)
                continue;
            if (!(item->flags() & Qt::ItemIsEnabled))
                continue;
            selectedPages.append(item->data(Qt::UserRole).toInt());
        }

        std::sort(selectedPages.begin(), selectedPages.end());
        selectedPages.erase(std::unique(selectedPages.begin(), selectedPages.end()), selectedPages.end());

        if (selectedPages.size() < 2)
        {
            logXAxisTrace(QString("link dialog ignored selection count=%1").arg(selectedPages.size()));
            return;
        }

        workingGroups.append(selectedPages);
        QStringList pageTexts;
        for (int pageIndex : selectedPages)
            pageTexts.append(QString::number(pageIndex));
        logXAxisTrace(QString("link dialog add working group pages=[%1] groups=%2")
                      .arg(pageTexts.join(',')).arg(workingGroups.size()));
        rebuildWidgets();
    });

    connect(btnRemove, &QPushButton::clicked, &dlg, [&, this]()
    {
        auto* item = treeLinked->currentItem();
        if (!item)
            return;

        const int pageIndex = item->data(0, Qt::UserRole).toInt();
        const int groupIndex = item->data(0, Qt::UserRole + 1).toInt();
        if (groupIndex < 0 || groupIndex >= workingGroups.size())
            return;

        if (pageIndex < 0)
        {
            logXAxisTrace(QString("link dialog remove working group=%1").arg(groupIndex));
            workingGroups.removeAt(groupIndex);
        }
        else
        {
            logXAxisTrace(QString("link dialog remove page=%1 from working group=%2")
                          .arg(pageIndex).arg(groupIndex));
            workingGroups[groupIndex].removeAll(pageIndex);
            if (workingGroups[groupIndex].size() < 2)
                workingGroups.removeAt(groupIndex);
        }

        rebuildWidgets();
    });

    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    rebuildWidgets();

    if (dlg.exec() != QDialog::Accepted)
    {
        logXAxisTrace("link dialog cancelled");
        return;
    }

    m_linkedXAxisGroups = workingGroups;
    cleanupLinkedXAxisGroups();
    logXAxisTrace(QString("link dialog accepted groups=%1").arg(m_linkedXAxisGroups.size()));
}

void UI::createMenu()
{
    /** Generating MainWindow part */

    auto* fileMenu = menuBar()->addMenu("文件");
    auto* openCSV = fileMenu->addAction("载入多个CSV文件");
    connect(openCSV, &QAction::triggered, this, &UI::onLoadCSVClicked);
    auto* openFolder = fileMenu->addAction("载入多个文件夹下的全部CSV文件");
    auto* openBinary = fileMenu->addAction("载入JSON+二进制文件");
    connect(openFolder, &QAction::triggered, this, &UI::onLoadFolderClicked);
    fileMenu->addSeparator();
    auto* clearAll = fileMenu->addAction("清空全部数据");
    connect(clearAll, &QAction::triggered, this, [this]()
    {
        m_viewer.Clear();
        m_dataTree->clear();
        m_xAxisLabel->setText("X: (none)");
    });

    auto* settingsMenu = menuBar()->addMenu(QString::fromUtf8("设置"));
    auto* aliasAction = settingsMenu->addAction(QString::fromUtf8("自动重命名数据"));
    connect(aliasAction, &QAction::triggered, this, &UI::showAliasDialog);

    m_actionAutoGrouping = settingsMenu->addAction(QString::fromUtf8("自动分组"));
    m_actionAutoGrouping->setCheckable(true);
    m_actionAutoGrouping->setChecked(m_autoGroupingEnabled);
    connect(m_actionAutoGrouping, &QAction::toggled, this, [this](bool checked)
    {
        m_autoGroupingEnabled = checked;
        if (m_viewer.GetDataManager().GetColumnCount() > 0)
            statusBar()->showMessage(QString::fromUtf8("自动分组设置已更新，将在下次读取数据后生效"), 4000);
    });

    auto* xAxisSettingAction = settingsMenu->addAction(QString::fromUtf8("默认X轴设置"));
    connect(xAxisSettingAction, &QAction::triggered, this, [this]()
    {
        auto& dm = m_viewer.GetDataManager();
        auto rulesCopy = dm.GetXAxisRules();
        XAxisDialog dlg(rulesCopy, this);
        if (dlg.exec() == QDialog::Accepted)
        {
            auto newRules = dlg.rules();
            dm.SetXAxisRules(newRules);
            // 保存到 user/xaxis.json
            QString jsonPath = QCoreApplication::applicationDirPath() + "/user/xaxis.json";
            dm.SaveXAxisRules(jsonPath.toStdString());
        }
    });

    settingsMenu->addSeparator();

    auto* logMenu = settingsMenu->addMenu(QString::fromUtf8(u8"日志"));
    for (const auto& info : viewer::trace::categories())
    {
        auto* action = logMenu->addAction(QString::fromUtf8(info.displayNameUtf8));
        action->setCheckable(true);
        action->setChecked(viewer::trace::isEnabled(info.category));
        connect(action, &QAction::toggled, this, [this, category = info.category](bool enabled)
        {
            viewer::trace::setEnabled(category, enabled);
            logOperationTrace(QString("log setting changed category=%1 enabled=%2")
                              .arg(QString::fromLatin1(viewer::trace::categoryInfo(category).settingsKey))
                              .arg(enabled));
        });
    }

    settingsMenu->addSeparator();

    auto* downsampleAction = settingsMenu->addAction(QString::fromUtf8("自适应降采样"));
    downsampleAction->setCheckable(true);
    downsampleAction->setChecked(m_adaptiveDownsampling);
    connect(downsampleAction, &QAction::toggled, this, [this](bool checked)
    {
        m_adaptiveDownsampling = checked;
        viewer::QCPColumnGraph::s_adaptiveSamplingEnabled = checked;
    });

    m_actionOpenGl = settingsMenu->addAction("OpenGL 绘制");
    m_actionOpenGl->setCheckable(true);
    m_actionOpenGl->setChecked(m_openglEnabled);
    connect(m_actionOpenGl, &QAction::toggled, this, [this](bool checked)
    {
        applyOpenGlDrawingMode(checked);
    });

    auto* antiAliasingAction = settingsMenu->addAction(QString::fromUtf8("曲线抗锯齿"));
    antiAliasingAction->setCheckable(true);
    antiAliasingAction->setChecked(m_antiAliasingEnabled);
    connect(antiAliasingAction, &QAction::toggled, this, [this](bool checked)
    {
        m_antiAliasingEnabled = checked;
        viewer::QCPColumnGraph::s_antiAliasingEnabled = checked;
    });

    auto* aboutAction = menuBar()->addAction("关于");
    connect(aboutAction, &QAction::triggered, this, [this]()
    {
        AboutDialog dlg(this);
        dlg.exec();
    });
}

// ============================================================
// 统一图标加载管线
// ============================================================

/// 根据 IconIdx 从 g_iconTable 加载原始 SVG 文本（支持 Base64 / SvgFile 两种来源）
static QString loadIconRaw(IconIdx id)
{
	for (int i = 0; i < g_iconTableCount; ++i)
	{
		if (g_iconTable[i].id != id)
			continue;

		const IconEntry& entry = g_iconTable[i];

		if (entry.source == IconSource::Base64)
		{
			// 解码 base64 data URI → SVG 文本
			QString dataUri = QString::fromUtf8(entry.data);
			QString base64Part = dataUri.section(',', 1);
			QByteArray svgBytes = QByteArray::fromBase64(base64Part.toUtf8());
			return QString::fromUtf8(svgBytes);
		}
		else // IconSource::SvgFile
		{
			// 从 Qt 资源系统或文件系统读取 .svg 文件（QFile 支持 :/ 资源路径）
			QString path = QString::fromUtf8(entry.data);
			QFile file(path);
			if (file.open(QIODevice::ReadOnly))
			{
				QByteArray data = file.readAll();
				file.close();
				return QString::fromUtf8(data);
			}
		}
		break;
	}
	return QString();
}

/// 三色标记系统：根据 stroke/fill 颜色值分类处理
///   kColorStroke → 描边反差色（深主题→浅灰, 浅主题→深色）
///   kColorFill   → 填充相容色（深主题→深底, 浅主题→浅底）
///   其余颜色     → 保持不变（彩色强调色）
static QString normalizeSvgColors(const QString& svgText, bool darkMode)
{
	// 主题色映射表
	const QString strokeColor = darkMode ? "#D0D0D0" : "#1A1A1A";  // 反差
	const QString fillColor   = darkMode ? "#2D2D2D" : "#F0F0F0";  // 相容

	// 归一化 #RGB → #RRGGBB，用于兼容短十六进制比较
	auto expandHex = [](const QString& hex) -> QString {
		if (hex.length() == 7) return hex;             // #RRGGBB
		if (hex.length() == 4 && hex[0] == '#') {      // #RGB → #RRGGBB
			return QString("#%1%1%2%2%3%3").arg(hex[1]).arg(hex[2]).arg(hex[3]);
		}
		return hex;
	};
	const QString expandedStroke = expandHex(QLatin1String(kColorStroke));
	const QString expandedFill   = expandHex(QLatin1String(kColorFill));

	QString result = svgText;

	/// 匹配所有 stroke / fill 颜色值
	///   支持两种语法：HTML属性 stroke="#000" 和 CSS样式 stroke:#000;
	QRegularExpression regex(R"((stroke|fill)[\s]*[=:][\s]*\"?(#[0-9a-fA-F]{3,6})\"?[\s;]?)");
	QRegularExpressionMatchIterator it = regex.globalMatch(result);

	// 收集所有匹配 (位置, 原始长度, 颜色值)，统一从后往前替换
	struct Match {
		int pos;
		int len;
		QString color;
	};
	QList<Match> matches;
	while (it.hasNext())
	{
		QRegularExpressionMatch m = it.next();
		Match match;
		match.pos   = m.capturedStart(2);
		match.len   = m.capturedLength(2);
		match.color = m.captured(2);
		matches.append(match);
	}

	// 按位置从后往前排序，避免替换后位置偏移
	std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
		return a.pos > b.pos;  // 降序
	});

	for (const Match& m : matches)
	{
		QString hexExpanded = expandHex(m.color);
		QString replacement;
		if (hexExpanded.compare(expandedStroke, Qt::CaseInsensitive) == 0)
			replacement = strokeColor;
		else if (hexExpanded.compare(expandedFill, Qt::CaseInsensitive) == 0)
			replacement = fillColor;
		else
			continue;  // 彩色或其他颜色，保持不变

		result.replace(m.pos, m.len, replacement);
	}

	return result;
}

/// 将 SVG 文本渲染为 DPI 感知的 QPixmap → QIcon
static QIcon renderSvgToIcon(const QString& svgText, int logicalSize = 24)
{
	QByteArray svgBytes = svgText.toUtf8();

	qreal dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
	int physicalSize = qRound(logicalSize * dpr);

	QPixmap pixmap(physicalSize, physicalSize);
	pixmap.fill(Qt::transparent);

	QSvgRenderer renderer(svgBytes);
	QPainter painter(&pixmap);
	renderer.render(&painter);
	painter.end();

	pixmap.setDevicePixelRatio(dpr);
	return QIcon(pixmap);
}

/// 统一入口：根据 IconIdx 加载图标（自动查表 → 颜色标准化 → DPI 渲染）
static QIcon createIcon(IconIdx id, int logicalSize = 36)
{
	QString svgText = loadIconRaw(id);
	if (svgText.isEmpty())
		return QIcon();

	bool dark = isSystemInDark();
	QString normalized = normalizeSvgColors(svgText, dark);
	return renderSvgToIcon(normalized, logicalSize);
}

// ============================================================
// 工具栏创建（表驱动 + group 自动分隔）
// ============================================================

void UI::createToolbar()
{
	ui.mainToolBar->setIconSize(QSize(36, 36));  // 1.5x 默认 24px

	QAction* action_loadcsv  = nullptr;
    QAction* action_loadfolder = nullptr;
	QAction* action_clearall = nullptr;
	QAction* action_varrename = nullptr;
    QAction* action_newplot = nullptr;
    QAction* action_gridview = nullptr;
    QAction* action_rowview = nullptr;
    QAction* action_linkx = nullptr;

	uint8_t lastGroup = 0xFF;  // 跟踪上一个图标的 group，换组时自动插入分隔符
	bool   firstItem  = true;

	for (int i = 0; i < g_iconTableCount; ++i)
	{
		const IconEntry& entry = g_iconTable[i];

		// 换组时插入分隔符（第一个图标前不插入）
		if (!firstItem && entry.group != lastGroup)
			ui.mainToolBar->addSeparator();

		QIcon icon = createIcon(entry.id);
		auto* action = new QAction(icon, entry.tooltip, this);
        if (entry.id == IconIdx::GRIDVIEW || entry.id == IconIdx::ROWVIEW)
            action->setCheckable(true);

		// 记录需要连接信号的 action
		switch (entry.id)
		{
		case IconIdx::LOADCSV:  action_loadcsv  = action; break;
        case IconIdx::LOADFOLDER: action_loadfolder = action; break;
		case IconIdx::CLEAR:    action_clearall = action; break;
		case IconIdx::VARRENAME: action_varrename = action; break;
        case IconIdx::NEWPLOT: action_newplot = action; break;
        case IconIdx::GRIDVIEW: action_gridview = action; break;
        case IconIdx::ROWVIEW: action_rowview = action; break;
        case IconIdx::LINKX: action_linkx = action; break;
		default: break;
		}

		ui.mainToolBar->addAction(action);

		lastGroup = entry.group;
		firstItem = false;
	}

	///< Connect buttons to slots
	if (action_loadcsv)
		connect(action_loadcsv, &QAction::triggered, this, &UI::onLoadCSVClicked);
    if (action_loadfolder)
        connect(action_loadfolder, &QAction::triggered, this, &UI::onLoadFolderClicked);
	if (action_clearall)
		connect(action_clearall, &QAction::triggered, this, [this]()
		{
			m_viewer.Clear();
			m_dataTree->clear();
			m_xAxisLabel->setText("X: (none)");
		});
	if (action_varrename)
		connect(action_varrename, &QAction::triggered, this, &UI::showAliasDialog);

    m_actionNewPlot = action_newplot;
    m_actionGridView = action_gridview;
    m_actionRowView = action_rowview;
    m_actionLinkX = action_linkx;

    if (m_actionNewPlot)
        connect(m_actionNewPlot, &QAction::triggered, this, [this]()
        {
            m_viewer.GetPlotManager().addPage();
        });

    if (m_actionGridView)
        connect(m_actionGridView, &QAction::toggled, this, [this](bool checked)
        {
            auto& pm = m_viewer.GetPlotManager();
            if (checked)
                pm.setLayoutMode(viewer::PlotLayoutMode::Grid);
            else if (pm.isLayoutMode(viewer::PlotLayoutMode::Grid))
                pm.setLayoutMode(viewer::PlotLayoutMode::Tabbed);
        });

    if (m_actionRowView)
        connect(m_actionRowView, &QAction::toggled, this, [this](bool checked)
        {
            auto& pm = m_viewer.GetPlotManager();
            if (checked)
                pm.setLayoutMode(viewer::PlotLayoutMode::Row);
            else if (pm.isLayoutMode(viewer::PlotLayoutMode::Row))
                pm.setLayoutMode(viewer::PlotLayoutMode::Tabbed);
        });

    if (m_actionLinkX)
        connect(m_actionLinkX, &QAction::triggered, this, &UI::showLinkXAxisDialog);

    updatePlotLayoutActions(m_viewer.GetPlotManager().layoutMode());
}

void UI::updatePlotLayoutActions(viewer::PlotLayoutMode mode)
{
    if (m_actionGridView)
    {
        m_actionGridView->blockSignals(true);
        m_actionGridView->setChecked(mode == viewer::PlotLayoutMode::Grid);
        m_actionGridView->blockSignals(false);
    }

    if (m_actionRowView)
    {
        m_actionRowView->blockSignals(true);
        m_actionRowView->setChecked(mode == viewer::PlotLayoutMode::Row);
        m_actionRowView->blockSignals(false);
    }
}

void UI::setPlotPageBaseChrome(int pageIndex, bool toolbarVisible, bool exprVisible)
{
    m_pageToolbarBaseVisible[pageIndex] = toolbarVisible;
    m_pageExprBaseVisible[pageIndex] = exprVisible;
}

void UI::updatePlotPageChromeForLayout(viewer::PlotLayoutMode mode)
{
    const QList<int> pageIndices = m_pageDocks.keys();
    for (int pageIndex : pageIndices)
    {
        auto* container = getPlotContainer(pageIndex);
        if (!container)
            continue;

        auto* vbox = container->findChild<QVBoxLayout*>();
        if (!vbox)
            continue;

        const bool forceHide = (mode != viewer::PlotLayoutMode::Tabbed);
        const bool toolbarVisible = forceHide ? false : m_pageToolbarBaseVisible.value(pageIndex, true);
        const bool exprVisible = forceHide ? false : m_pageExprBaseVisible.value(pageIndex, true);

        if (vbox->count() >= 1)
        {
            if (auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget()))
                toolbar->setVisible(toolbarVisible);
        }
        if (vbox->count() >= 3)
        {
            if (auto* exprBar = qobject_cast<QWidget*>(vbox->itemAt(2)->widget()))
                exprBar->setVisible(exprVisible);
        }
    }
}

void UI::clearLayoutPlaceholders()
{
    if (!m_plotDockManager || m_layoutPlaceholderDocks.isEmpty())
        return;

    const auto placeholders = m_layoutPlaceholderDocks;
    m_layoutPlaceholderDocks.clear();
    for (auto* dock : placeholders)
    {
        if (dock)
            m_plotDockManager->removeDockWidget(dock);
    }
}

void UI::applyPlotLayoutMode(viewer::PlotLayoutMode mode)
{
    logLayoutTrace(QString("apply layout enter mode=%1 pages=%2 plotDockManager=0x%3 shuttingDown=%4 savedTabbedState=%5")
                   .arg(static_cast<int>(mode)).arg(m_pageDocks.size())
                   .arg(reinterpret_cast<quintptr>(m_plotDockManager), 0, 16)
                   .arg(m_isShuttingDown).arg(m_hasSavedTabbedPlotLayoutState));
    if (!m_plotDockManager || m_isShuttingDown)
    {
        logLayoutTrace("apply layout aborted: unavailable manager or shutdown in progress");
        return;
    }

    if (mode == viewer::PlotLayoutMode::Tabbed)
    {
        restoreTabbedPlotLayout();
    }
    else
    {
        if (!m_hasSavedTabbedPlotLayoutState)
        {
            m_savedTabbedPlotLayoutState = m_plotDockManager->saveState();
            m_hasSavedTabbedPlotLayoutState = !m_savedTabbedPlotLayoutState.isEmpty();
        }

        if (mode == viewer::PlotLayoutMode::Grid)
            arrangePlotsInGridLayout();
        else
            arrangePlotsInRowLayout();
    }

    updatePlotPageChromeForLayout(mode);
    updatePlotLayoutActions(mode);
    logLayoutTrace(QString("apply layout leave mode=%1 pages=%2 placeholders=%3")
                   .arg(static_cast<int>(mode)).arg(m_pageDocks.size())
                   .arg(m_layoutPlaceholderDocks.size()));
}

void UI::restoreTabbedPlotLayout()
{
    if (!m_plotDockManager)
        return;

    const int activeIndex = m_viewer.GetPlotManager().activePageIndex();
    bool restored = false;
    logLayoutTrace(QString("restore tabbed enter activePage=%1 savedState=%2 stateBytes=%3")
                   .arg(activeIndex).arg(m_hasSavedTabbedPlotLayoutState)
                   .arg(m_savedTabbedPlotLayoutState.size()));

    clearLayoutPlaceholders();

    if (m_hasSavedTabbedPlotLayoutState && !m_savedTabbedPlotLayoutState.isEmpty())
    {
        m_rearrangingPlotLayout = true;
        restored = m_plotDockManager->restoreState(m_savedTabbedPlotLayoutState);
        m_rearrangingPlotLayout = false;
    }

    if (!restored)
    {
        logLayoutTrace("restore tabbed saved state unavailable/failed; normalizing docks");
        normalizeAllPlotDocksToMainContainer();
    }

    m_savedTabbedPlotLayoutState.clear();
    m_hasSavedTabbedPlotLayoutState = false;

    if (activeIndex >= 0)
    {
        if (auto* dock = m_pageDocks.value(activeIndex, nullptr))
        {
            if (auto* area = dock->dockAreaWidget())
                area->setCurrentDockWidget(dock);
            m_plotDockManager->setDockWidgetFocused(dock);
        }
    }
    logLayoutTrace(QString("restore tabbed leave activePage=%1 restoredState=%2")
                   .arg(activeIndex).arg(restored));
}

void UI::normalizeAllPlotDocksToMainContainer()
{
    if (!m_plotDockManager)
        return;

    QList<int> pageIndices = m_pageDocks.keys();
    std::sort(pageIndices.begin(), pageIndices.end());
    if (pageIndices.isEmpty())
        return;

    logLayoutTrace(QString("normalize tabbed docks enter pages=%1").arg(pageIndices.size()));

    const int activeIndex = m_viewer.GetPlotManager().activePageIndex();
    m_rearrangingPlotLayout = true;

    clearLayoutPlaceholders();

    for (int pageIndex : pageIndices)
    {
        if (auto* dock = m_pageDocks.value(pageIndex, nullptr))
            m_plotDockManager->removeDockWidget(dock);
    }

    ads::CDockAreaWidget* firstArea = nullptr;
    for (int pageIndex : pageIndices)
    {
        auto* dock = m_pageDocks.value(pageIndex, nullptr);
        if (!dock)
            continue;

        if (!firstArea)
            firstArea = m_plotDockManager->addDockWidgetToContainer(ads::CenterDockWidgetArea, dock, m_plotDockManager);
        else
            m_plotDockManager->addDockWidgetTabToArea(dock, firstArea);
    }

    m_rearrangingPlotLayout = false;

    if (activeIndex >= 0)
    {
        if (auto* dock = m_pageDocks.value(activeIndex, nullptr))
        {
            if (auto* area = dock->dockAreaWidget())
                area->setCurrentDockWidget(dock);
            m_plotDockManager->setDockWidgetFocused(dock);
        }
    }
    logLayoutTrace(QString("normalize tabbed docks leave pages=%1 activePage=%2 firstArea=0x%3")
                   .arg(pageIndices.size()).arg(activeIndex)
                   .arg(reinterpret_cast<quintptr>(firstArea), 0, 16));
}

void UI::arrangePlotsInRowLayout()
{
    if (!m_plotDockManager)
        return;

    QList<int> pageIndices = m_pageDocks.keys();
    std::sort(pageIndices.begin(), pageIndices.end());
    if (pageIndices.isEmpty())
        return;

    logLayoutTrace(QString("arrange row enter pages=%1").arg(pageIndices.size()));

    const int activeIndex = m_viewer.GetPlotManager().activePageIndex();
    m_rearrangingPlotLayout = true;

    clearLayoutPlaceholders();

    for (int pageIndex : pageIndices)
    {
        if (auto* dock = m_pageDocks.value(pageIndex, nullptr))
            m_plotDockManager->removeDockWidget(dock);
    }

    bool firstDock = true;
    for (int pageIndex : pageIndices)
    {
        auto* dock = m_pageDocks.value(pageIndex, nullptr);
        if (!dock)
            continue;

        if (firstDock)
        {
            m_plotDockManager->addDockWidgetToContainer(ads::CenterDockWidgetArea, dock, m_plotDockManager);
            firstDock = false;
        }
        else
        {
            m_plotDockManager->addDockWidgetToContainer(ads::BottomDockWidgetArea, dock, m_plotDockManager);
        }
    }

    m_rearrangingPlotLayout = false;

    if (activeIndex >= 0)
    {
        if (auto* dock = m_pageDocks.value(activeIndex, nullptr))
            m_plotDockManager->setDockWidgetFocused(dock);
    }
    logLayoutTrace(QString("arrange row leave pages=%1 activePage=%2")
                   .arg(pageIndices.size()).arg(activeIndex));
}

void UI::arrangePlotsInGridLayout()
{
    if (!m_plotDockManager)
        return;

    QList<int> pageIndices = m_pageDocks.keys();
    std::sort(pageIndices.begin(), pageIndices.end());
    if (pageIndices.isEmpty())
        return;

    const int activeIndex = m_viewer.GetPlotManager().activePageIndex();
    const int plotCount = pageIndices.size();
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(plotCount))));
    const int rows = static_cast<int>(std::ceil(static_cast<double>(plotCount) / static_cast<double>(cols)));
    logLayoutTrace(QString("arrange grid enter pages=%1 rows=%2 cols=%3 activePage=%4")
                   .arg(plotCount).arg(rows).arg(cols).arg(activeIndex));

    m_rearrangingPlotLayout = true;

    clearLayoutPlaceholders();

    for (int pageIndex : pageIndices)
    {
        if (auto* dock = m_pageDocks.value(pageIndex, nullptr))
            m_plotDockManager->removeDockWidget(dock);
    }

    QList<QList<ads::CDockWidget*>> gridRows;
    int cursor = 0;
    for (int row = 0; row < rows; ++row)
    {
        QList<ads::CDockWidget*> rowDocks;
        for (int col = 0; col < cols; ++col)
        {
            ads::CDockWidget* dock = nullptr;
            if (cursor < plotCount)
            {
                dock = m_pageDocks.value(pageIndices[cursor], nullptr);
                ++cursor;
            }
            else
            {
                dock = new ads::CDockWidget(QString());
                dock->setObjectName(QString("layout_placeholder_%1_%2").arg(row).arg(col));
                dock->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
                auto* filler = new QWidget();
                filler->setStyleSheet("background: transparent;");
                dock->setWidget(filler);
                m_layoutPlaceholderDocks.append(dock);
            }
            rowDocks.append(dock);
        }
        gridRows.append(rowDocks);
    }

    QList<ads::CDockAreaWidget*> rowAreas;
    for (int row = 0; row < rows; ++row)
    {
        if (gridRows[row].isEmpty() || !gridRows[row][0])
            continue;

        ads::CDockAreaWidget* rowArea = nullptr;
        if (row == 0)
            rowArea = m_plotDockManager->addDockWidgetToContainer(ads::CenterDockWidgetArea, gridRows[row][0], m_plotDockManager);
        else
            rowArea = m_plotDockManager->addDockWidgetToContainer(ads::BottomDockWidgetArea, gridRows[row][0], m_plotDockManager);

        rowAreas.append(rowArea);
    }

    for (int row = 0; row < rows; ++row)
    {
        ads::CDockAreaWidget* rowArea = (row < rowAreas.size()) ? rowAreas[row] : nullptr;
        if (!rowArea)
            continue;

        for (int col = 1; col < cols; ++col)
        {
            auto* dock = gridRows[row][col];
            if (!dock)
                continue;

            rowArea = m_plotDockManager->addDockWidget(ads::RightDockWidgetArea, dock, rowArea);
        }

        QList<int> rowSizes;
        for (int col = 0; col < cols; ++col)
            rowSizes.append(1);
        m_plotDockManager->setSplitterSizes(rowAreas[row], rowSizes);
    }

    // 网格布局构建完成后，再统一设置纵向根 splitter 的尺寸，
    // 避免 QADS 按插入顺序保留不均匀的首行高度。
    if (auto* rootSplitter = findCommonVerticalPlotSplitter(rowAreas, rowAreas.size()))
    {
        QList<int> columnSizes;
        for (int row = 0; row < rowAreas.size(); ++row)
        {
            rootSplitter->setStretchFactor(row, 1);
            columnSizes.append(1);
        }
        rootSplitter->setSizes(columnSizes);
    }

    m_rearrangingPlotLayout = false;

    if (activeIndex >= 0)
    {
        if (auto* dock = m_pageDocks.value(activeIndex, nullptr))
            m_plotDockManager->setDockWidgetFocused(dock);
    }
    logLayoutTrace(QString("arrange grid leave pages=%1 rows=%2 cols=%3 placeholders=%4")
                   .arg(plotCount).arg(rows).arg(cols).arg(m_layoutPlaceholderDocks.size()));
}

// ============================================================
// 内层 DockManager 辅助方法实现
// ============================================================

void UI::hideDockAreaTitleBar(ads::CDockWidget* dock)
{
    if (m_isShuttingDown)
        return;

    auto* area = dock ? dock->dockAreaWidget() : nullptr;
    auto* titleBar = area ? area->titleBar() : nullptr;
    if (!area || !titleBar)
        return;

    area->setDockAreaFlag(ads::CDockAreaWidget::HideSingleWidgetTitleBar, true);
    titleBar->hide();
    titleBar->setFixedHeight(0);
}

void UI::hideFixedDockTitleBars()
{
    hideDockAreaTitleBar(m_plotDock);
    hideDockAreaTitleBar(m_dataDock);
    hideDockAreaTitleBar(m_bookmarkDock);
}

QCustomPlot* UI::getPlot(int pageIndex) const
{
	auto* dock = m_pageDocks.value(pageIndex, nullptr);
	if (!dock) return nullptr;
	auto* container = dock->widget();
	return container ? container->findChild<QCustomPlot*>() : nullptr;
}

QWidget* UI::getPlotContainer(int pageIndex) const
{
	auto* dock = m_pageDocks.value(pageIndex, nullptr);
	return dock ? dock->widget() : nullptr;
}

int UI::plotPageCount() const
{
	return m_pageDocks.size();
}

int UI::activePlotPage() const
{
	if (!m_plotDockManager) return -1;
	auto* focused = m_plotDockManager->focusedDockWidget();
	if (!focused) return -1;
	return m_pageDocks.key(focused, -1);
}

ads::CDockWidget* UI::addPlotPageDock(int pageIndex, QWidget* container, const QString& title)
{
	auto* dock = new ads::CDockWidget(title);
	dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
	dock->setFeature(ads::CDockWidget::DockWidgetClosable, true);
	dock->setWidget(container);

	m_pageDocks[pageIndex] = dock;

	// 当 dock widget 以任何方式被销毁时，同步清理映射表
	connect(dock, &QObject::destroyed, this, [this, dock]()
	{
		int pageIndex = m_pageDocks.key(dock, -1);
		if (pageIndex >= 0)
			 m_pageDocks.remove(pageIndex);
	});

	// 从 QADS 内部 map 查找已存在的目标区域。必须拷贝 map 再迭代：
	// removeDockWidget 会异步析构 dock，若持有引用迭代可能遇到残留悬空条目。
	ads::CDockAreaWidget* targetArea = nullptr;
	if (m_pendingDockTargetPage >= 0)
	{
		auto* targetDock = m_pageDocks.value(m_pendingDockTargetPage, nullptr);
		if (targetDock && targetDock != dock && targetDock->dockAreaWidget())
			targetArea = targetDock->dockAreaWidget();
	}
	if (!targetArea)
	{
		const auto dwMap = m_plotDockManager->dockWidgetsMap();
		for (auto it = dwMap.begin(); it != dwMap.end(); ++it)
		{
			if (it.value() != dock && it.value()->dockAreaWidget())
			{
				targetArea = it.value()->dockAreaWidget();
				break;
			}
		}
	}

	if (targetArea && m_pendingDockTargetPage >= 0 && m_pendingDockArea != ads::CenterDockWidgetArea)
		m_plotDockManager->addDockWidget(m_pendingDockArea, dock, targetArea);
	else if (targetArea)
		m_plotDockManager->addDockWidgetTabToArea(dock, targetArea);
	else
		m_plotDockManager->addDockWidget(ads::CenterDockWidgetArea, dock);

	m_pendingDockTargetPage = -1;
	m_pendingDockArea = ads::CenterDockWidgetArea;

	m_pendingActivation = pageIndex;
	return dock;
}

void UI::cleanupPlotPageState(int pageIndex, ads::CDockWidget* dock)
{
    logShutdownTrace(QString("cleanupPlotPageState enter page=%1 dock=%2 shuttingDown=%3")
                         .arg(pageIndex)
                         .arg(reinterpret_cast<quintptr>(dock), 0, 16)
                         .arg(m_isShuttingDown));
    QWidget* container = dock ? dock->widget() : getPlotContainer(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    logShutdownTrace(QString("cleanupPlotPageState objects page=%1 container=%2 plot=%3")
                         .arg(pageIndex)
                         .arg(reinterpret_cast<quintptr>(container), 0, 16)
                         .arg(reinterpret_cast<quintptr>(plot), 0, 16));

    if (plot)
    {
        if (!m_isShuttingDown && m_fftSelectRect && m_fftSelectRect->parentPlot() == plot)
        {
            plot->removeItem(m_fftSelectRect);
            m_fftSelectRect = nullptr;
            m_fftSelecting = false;
            m_fftPageIndex = -1;
        }

        m_plotToPageIndex.remove(plot);
        m_preSelTracers.remove(plot);
    }

    if (m_fftPageIndex == pageIndex)
    {
        m_fftSelecting = false;
        m_fftPageIndex = -1;
        m_fftSelectRect = nullptr;
    }

    auto rects = m_highlightRects.take(pageIndex);
    for (auto* rect : rects)
    {
        if (!m_isShuttingDown && rect && rect->parentPlot())
            rect->parentPlot()->removeItem(rect);
    }

    auto labels = m_highlightLabels.take(pageIndex);
    for (auto* label : labels)
    {
        if (!m_isShuttingDown && label && label->parentPlot())
            label->parentPlot()->removeItem(label);
    }

    auto connIt = m_highlightReplotConns.find(pageIndex);
    if (connIt != m_highlightReplotConns.end())
    {
        disconnect(connIt.value());
        m_highlightReplotConns.erase(connIt);
    }

    m_exprLineEdits.remove(pageIndex);
    m_toolbarCombos.remove(pageIndex);
    m_pageToolbarBaseVisible.remove(pageIndex);
    m_pageExprBaseVisible.remove(pageIndex);
    m_fftMagCols.erase(pageIndex);
    m_fftFreqCols.erase(pageIndex);
    logShutdownTrace(QString("cleanupPlotPageState leave page=%1").arg(pageIndex));
}

void UI::removeAllPlotDocksForShutdown()
{
    logShutdownTrace(QString("removeAllPlotDocksForShutdown enter pageDocks=%1 plotDockMgr=%2")
                         .arg(m_pageDocks.size())
                         .arg(reinterpret_cast<quintptr>(m_plotDockManager), 0, 16));

    if (!m_plotDockManager)
    {
        logShutdownTrace("removeAllPlotDocksForShutdown no plot dock manager");
        return;
    }

    m_syncingPlotRemoval = true;
    const auto pageIndices = m_pageDocks.keys();
    for (int pageIndex : pageIndices)
    {
        logShutdownTrace(QString("removeAllPlotDocksForShutdown remove page=%1").arg(pageIndex));
        removePlotPageDock(pageIndex);
    }
    m_syncingPlotRemoval = false;

    logShutdownTrace(QString("removeAllPlotDocksForShutdown leave remaining=%1").arg(m_pageDocks.size()));
}

template <typename T>
static void reindexQHashAfterRemoval(QHash<int, T>& map, int removedPageIndex)
{
    QHash<int, T> shifted;
    for (auto it = map.begin(); it != map.end(); ++it)
    {
        int key = it.key();
        if (key == removedPageIndex)
            continue;
        if (key > removedPageIndex)
            --key;
        shifted.insert(key, it.value());
    }
    map = std::move(shifted);
}

template <typename T>
static void reindexStdMapAfterRemoval(std::unordered_map<int, T>& map, int removedPageIndex)
{
    std::unordered_map<int, T> shifted;
    for (auto& entry : map)
    {
        int key = entry.first;
        if (key == removedPageIndex)
            continue;
        if (key > removedPageIndex)
            --key;
        shifted.emplace(key, std::move(entry.second));
    }
    map = std::move(shifted);
}

void UI::reindexPlotPageStateAfterRemoval(int removedPageIndex)
{
    if (removedPageIndex < 0)
        return;

    reindexQHashAfterRemoval(m_pageDocks, removedPageIndex);
    reindexQHashAfterRemoval(m_exprLineEdits, removedPageIndex);
    reindexQHashAfterRemoval(m_toolbarCombos, removedPageIndex);
    reindexQHashAfterRemoval(m_pageToolbarBaseVisible, removedPageIndex);
    reindexQHashAfterRemoval(m_pageExprBaseVisible, removedPageIndex);
    reindexQHashAfterRemoval(m_highlightRects, removedPageIndex);
    reindexQHashAfterRemoval(m_highlightLabels, removedPageIndex);
    for (auto it = m_highlightReplotConns.begin(); it != m_highlightReplotConns.end(); ++it)
        disconnect(it.value());
    m_highlightReplotConns.clear();
    reindexStdMapAfterRemoval(m_fftMagCols, removedPageIndex);
    reindexStdMapAfterRemoval(m_fftFreqCols, removedPageIndex);

    const auto highlightPages = m_highlightRects.keys();
    for (int pageIndex : highlightPages)
    {
        if (pageIndex >= 0 && pageIndex < plotPageCount())
            renderHighlights(pageIndex);
    }

    for (auto it = m_plotToPageIndex.begin(); it != m_plotToPageIndex.end(); ++it)
    {
        if (it.value() > removedPageIndex)
            it.value() = it.value() - 1;
    }

    m_viewer.GetCursorManager().shiftPageIndicesAfterRemoval(removedPageIndex);

    if (m_fftPageIndex > removedPageIndex)
        --m_fftPageIndex;
    else if (m_fftPageIndex == removedPageIndex)
        m_fftPageIndex = -1;

    if (m_pendingActivation > removedPageIndex)
        --m_pendingActivation;
    else if (m_pendingActivation == removedPageIndex)
        m_pendingActivation = -1;
}
void UI::removePlotPageDock(int pageIndex)
{
    logShutdownTrace(QString("removePlotPageDock enter page=%1 pageDocks=%2")
                         .arg(pageIndex)
                         .arg(m_pageDocks.size()));
    auto* dock = m_pageDocks.take(pageIndex);
    if (dock)
    {
        m_lastRemovedPageIndex = pageIndex;
        cleanupPlotPageState(pageIndex, dock);
        logShutdownTrace(QString("removePlotPageDock removeDockWidget page=%1 dock=%2")
                             .arg(pageIndex)
                             .arg(reinterpret_cast<quintptr>(dock), 0, 16));
        m_plotDockManager->removeDockWidget(dock);
    }
    else
    {
        logShutdownTrace(QString("removePlotPageDock missing dock page=%1").arg(pageIndex));
    }
    logShutdownTrace(QString("removePlotPageDock leave page=%1").arg(pageIndex));
}

void UI::connectInnerDockSignals()
{
	if (m_innerDockSignalsConnected) return;
	m_innerDockSignalsConnected = true;

	// QADS 关闭 dock widget 时：先从映射表清除，再通知 PlotManager。
	// 先 remove 确保 removePlotPageDock 的 take() 返回 nullptr → 不重复调用 removeDockWidget。
	connect(m_plotDockManager, &ads::CDockManager::dockWidgetAboutToBeRemoved, this,
		[this](ads::CDockWidget* dw)
		{
            logShutdownTrace(QString("dockWidgetAboutToBeRemoved dw=%1 shuttingDown=%2 syncing=%3")
                                 .arg(reinterpret_cast<quintptr>(dw), 0, 16)
                                 .arg(m_isShuttingDown)
                                 .arg(m_syncingPlotRemoval));
            if (m_isShuttingDown || m_rearrangingPlotLayout)
                return;

			int pageIndex = m_pageDocks.key(dw, -1);
			if (pageIndex >= 0)
			{
                logShutdownTrace(QString("dockWidgetAboutToBeRemoved page=%1").arg(pageIndex));
				 m_lastRemovedPageIndex = pageIndex;
				cleanupPlotPageState(pageIndex, dw);
				 m_pageDocks.remove(pageIndex);
                if (!m_syncingPlotRemoval)
                {
				    auto& pm = m_viewer.GetPlotManager();
                    logShutdownTrace(QString("dockWidgetAboutToBeRemoved calling pm.removePage(%1)").arg(pageIndex));
				    pm.removePage(pageIndex);
                }
			}
            else
            {
                logShutdownTrace("dockWidgetAboutToBeRemoved page not found");
            }
		});

	// 连接内层 dock widget 聚焦变更信号
	connect(m_plotDockManager, &ads::CDockManager::focusedDockWidgetChanged, this,
		[this](ads::CDockWidget* old, ads::CDockWidget* now)
		{
            Q_UNUSED(old);
            if (m_isShuttingDown || m_rearrangingPlotLayout)
                return;
			if (!now) return;
			int pageIndex = m_pageDocks.key(now, -1);
			if (pageIndex >= 0)
			{
				auto& pm = m_viewer.GetPlotManager();
				if (pm.activePageIndex() != pageIndex)
					pm.setActivePage(pageIndex);
			}
		});

	// Fallback: 连接每个新 DockArea 的 currentChanged 信号
	// 确保在 drag-reorder / programmatic tab switch 等场景下也能同步
	connect(m_plotDockManager, &ads::CDockManager::dockAreaCreated, this,
		[this](ads::CDockAreaWidget* area)
		{
			connect(area, &ads::CDockAreaWidget::currentChanged, this,
                [this, area](int tabIndex)
                {
                    if (m_isShuttingDown || m_rearrangingPlotLayout)
                        return;

					auto* dw = area->dockWidget(tabIndex);
					if (!dw) return;
					int pageIndex = m_pageDocks.key(dw, -1);
					if (pageIndex >= 0)
					{
						auto& pm = m_viewer.GetPlotManager();
						if (pm.activePageIndex() != pageIndex)
							pm.setActivePage(pageIndex);
					}
				});
		});
}
