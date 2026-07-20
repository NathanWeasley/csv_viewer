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
#include <QSignalBlocker>
#include <qtimer.h>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "code_viewer/base/trace_logger.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <qdatetime.h>
#include <qtextstream.h>

extern bool isSystemInDark();

namespace
{
constexpr int kDataTreeKindRole = Qt::UserRole;
constexpr int kDataTreeNameRole = Qt::UserRole + 1;
constexpr int kDataTreeLeafKind = 2;

}

UI::UI(QWidget *parent)
    : QMainWindow(parent)
{
    logOperationTrace("UI constructor enter");
    ui.setupUi(this);

    // ---- QADS 全局配置（必须在任何 CDockManager 创建之前设置） ----
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::RetainTabSizeWhenCloseButtonHidden, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewIsDynamic, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewShowsContentPixmap, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    m_dockManager = new ads::CDockManager(ui.centralWidget);

    /** 先读取 Settings 状态（init/createMenu 中需要用到） */
    {
        QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);

        // 读取自适应降采样设置（默认 true）
        m_adaptiveDownsampling = settings.value("adaptiveDownsampling", true).toBool();
        viewer::QCPColumnGraph::s_adaptiveSamplingEnabled = m_adaptiveDownsampling;

        // 读取 OpenGL 设置（默认 true）
        m_openglEnabled = settings.value("openglEnabled", true).toBool();

        // 读取抗锯齿设置（默认 false）
        m_antiAliasingEnabled = settings.value("antiAliasing", false).toBool();
        viewer::QCPColumnGraph::s_antiAliasingEnabled = m_antiAliasingEnabled;

        // 读取自动分组设置（默认 false）
        m_autoGroupingEnabled = settings.value("autoGrouping", false).toBool();
    }

    init();

    /** Restore window geometry & docking state */
    {
        QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);

        if (settings.contains("geometry"))
        {
            restoreGeometry(settings.value("geometry").toByteArray());
        }

        if (settings.contains("dockingState"))
        {
            m_dockManager->restoreState(settings.value("dockingState").toByteArray());
            hideFixedDockTitleBars();
            QTimer::singleShot(0, this, [this]()
            {
                hideFixedDockTitleBars();
            });
        }
    }
    logOperationTrace(QString("UI constructor leave pages=%1").arg(plotPageCount()));
}

UI::~UI()
{
    logShutdownTrace("~UI enter");
    beginShutdownCleanup(true);
    logShutdownTrace("~UI leave");
}

void UI::init()
{
    logOperationTrace("UI init enter");
    setCentralWidget(m_dockManager);

    // ---- 初始化样式管理器 ----
    {
        auto& sm = m_viewer.GetStyleManager();
        bool dark = isSystemInDark();

        // 尝试从 user/style.json 加载
        QString stylePath = QCoreApplication::applicationDirPath() + "/user/style.json";
        if (!sm.load(stylePath.toStdString()))
        {
            sm.initializeDefaults(dark);
        }
    }

    createMenu();
    logOperationTrace("UI init menu created");
    createMain();
    logOperationTrace("UI init main docks created");

    // ---- 加载 X 轴规则 ----
    {
        QString jsonPath = QCoreApplication::applicationDirPath() + "/user/xaxis.json";
        m_viewer.GetDataManager().LoadXAxisRules(jsonPath.toStdString());
        logXAxisTrace(QString("default X-axis rules loaded path=\"%1\"").arg(jsonPath));
    }

    // ---- 加载变量自动重命名规则 ----
    loadAliasFile();

    // ---- 加载书签 ----
    loadBookmarkFile();

    createToolbar();
    createStatusbar();

    bindCursorManagerCallbacks();
    logOperationTrace("UI init leave callbacks bound");
}

void UI::saveState()
{
    auto& sm = m_viewer.GetStyleManager();
    QString stylePath = QCoreApplication::applicationDirPath() + "/user/style.json";
    sm.save(stylePath.toStdString());
    logOperationTrace(QString("style state saved path=\"%1\"").arg(stylePath));
}

void UI::beginShutdownCleanup(bool persistUiState)
{
    if (m_shutdownCleanupDone)
    {
        logShutdownTrace(QString("beginShutdownCleanup skipped persist=%1").arg(persistUiState));
        return;
    }

    m_shutdownCleanupDone = true;
    m_isShuttingDown = true;
    logShutdownTrace(QString("beginShutdownCleanup enter persist=%1 pageDocks=%2 plotDockMgr=%3 dockMgr=%4")
                         .arg(persistUiState)
                         .arg(m_pageDocks.size())
                         .arg(reinterpret_cast<quintptr>(m_plotDockManager), 0, 16)
                         .arg(reinterpret_cast<quintptr>(m_dockManager), 0, 16));

    if (persistUiState)
    {
        QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
        QSettings settings(configPath, QSettings::IniFormat);

        settings.setValue("geometry", saveGeometry());
        if (m_dockManager)
            settings.setValue("dockingState", m_dockManager->saveState());

        settings.setValue("adaptiveDownsampling", m_adaptiveDownsampling);
        settings.setValue("openglEnabled", m_openglEnabled);
        settings.setValue("antiAliasing", m_antiAliasingEnabled);
        settings.setValue("autoGrouping", m_autoGroupingEnabled);

        saveState();
        logShutdownTrace("beginShutdownCleanup saved UI state");
    }

    disconnectViewerCallbacks();
    logShutdownTrace("beginShutdownCleanup disconnected viewer callbacks");
    cleanupPlotOverlaysBeforeShutdown();
    logShutdownTrace("beginShutdownCleanup finished overlay cleanup");
    m_rearrangingPlotLayout = true;
    clearLayoutPlaceholders();
    m_rearrangingPlotLayout = false;
    removeAllPlotDocksForShutdown();
    logShutdownTrace("beginShutdownCleanup finished plot dock removal");

    if (m_plotDockManager)
    {
        disconnect(m_plotDockManager, nullptr, this, nullptr);
        logShutdownTrace("beginShutdownCleanup disconnected m_plotDockManager signals");
    }
    if (m_dockManager)
    {
        disconnect(m_dockManager, nullptr, this, nullptr);
        logShutdownTrace("beginShutdownCleanup disconnected m_dockManager signals");
    }
}

void UI::disconnectViewerCallbacks()
{
    logShutdownTrace("disconnectViewerCallbacks enter");
    auto& pm = m_viewer.GetPlotManager();
    pm.onXAxisChanged = nullptr;
    pm.onPageAdded = nullptr;
    pm.onPageAboutToRemove = nullptr;
    pm.onPageRemoved = nullptr;
    pm.onActivePageChanged = nullptr;
    pm.onDataItemAdded = nullptr;
    pm.onDataItemRemoved = nullptr;
    pm.onCleared = nullptr;
    pm.onLayoutModeChanged = nullptr;
    pm.onLegendVisibilityChanged = nullptr;
    pm.onLegendNeedReplot = nullptr;
    pm.onRectZoomStateChanged = nullptr;
    pm.onRescaleRequested = nullptr;
    pm.onSelectedDataItemChanged = nullptr;

    auto& cm = m_viewer.GetCursorManager();
    cm.onPreSelectionSet = nullptr;
    cm.onPreSelectionCleared = nullptr;
    cm.onCursorAdded = nullptr;
    cm.onCursorRemoved = nullptr;
    cm.onActiveCursorChanged = nullptr;

    m_viewer.GetStyleManager().onPaletteChanged = nullptr;
    logShutdownTrace("disconnectViewerCallbacks leave");
}

void UI::closeEvent(QCloseEvent* event)
{
    logShutdownTrace("closeEvent enter");
    beginShutdownCleanup(true);
    QMainWindow::closeEvent(event);
    logShutdownTrace("closeEvent leave");
}

void UI::logShutdownTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::Shutdown, message);
}

void UI::logPlotTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::PlotOpenGL, message);
}

void UI::logOperationTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::Operation, message);
}

void UI::logFileTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::FileIO, message);
}

void UI::logBookmarkTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::Bookmark, message);
}

void UI::logLayoutTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::Layout, message);
}

void UI::logXAxisTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::XAxis, message);
}

void UI::logExpressionTrace(const QString& message) const
{
    viewer::trace::write(viewer::trace::Category::Expression, message);
}

bool UI::configurePlotDrawingMode(QCustomPlot* plot, bool enabled) const
{
    if (!plot)
        return false;

    // CPU 模式恢复 QCustomPlot 原始的软件绘制路径。
    // OpenGL 模式统一交给 QCustomPlot 的 OpenGL 后端，不再单独拆分散点路径。
    plot->setOpenGl(enabled, 0);
    const bool active = plot->openGl();
    if (auto* overlayLayer = plot->layer("overlay"))
        overlayLayer->setMode(active ? QCPLayer::lmLogical : QCPLayer::lmBuffered);

    logPlotTrace(QString("configure drawing mode page=%1 openGl=%2 overlayMode=%3")
                 .arg(m_plotToPageIndex.value(plot, -1))
                 .arg(active)
                 .arg(plot->layer("overlay") && plot->layer("overlay")->mode() == QCPLayer::lmLogical ? "logical" : "buffered"));
    return active;
}

void UI::applyOpenGlDrawingMode(bool enabled)
{
    const auto pageIndices = m_pageDocks.keys();
    logPlotTrace(QString("apply drawing mode enter requestedOpenGl=%1 pages=%2")
                 .arg(enabled).arg(pageIndices.size()));
    bool rejected = false;
    for (int pageIndex : pageIndices)
    {
        auto* plot = getPlot(pageIndex);
        if (!plot)
            continue;

        const bool prevUpdatesEnabled = plot->updatesEnabled();
        plot->setUpdatesEnabled(false);
        const bool active = configurePlotDrawingMode(plot, enabled);
        rejected = rejected || (enabled && !active);
        plot->setUpdatesEnabled(prevUpdatesEnabled);

        if (prevUpdatesEnabled)
            plot->replot(QCustomPlot::rpQueuedRefresh);
    }

    if (rejected)
    {
        for (int pageIndex : pageIndices)
            configurePlotDrawingMode(getPlot(pageIndex), false);
        enabled = false;
        statusBar()->showMessage(
            QString::fromUtf8("当前显卡不适合此 OpenGL 绘制路径，已自动切换为软件绘制。"), 6000);
    }

    m_openglEnabled = enabled;
    if (m_actionOpenGl && m_actionOpenGl->isChecked() != enabled)
    {
        const QSignalBlocker blocker(m_actionOpenGl);
        m_actionOpenGl->setChecked(enabled);
    }
    logPlotTrace(QString("apply drawing mode leave activeOpenGl=%1 rejected=%2")
                 .arg(m_openglEnabled).arg(rejected));
}
// ============================================================
// 双击数据树项 → 添加到激活图窗
// ============================================================

void UI::plotDataColumnByName(const QString& dataName)
{
    logOperationTrace(QString("plot data request enter name=\"%1\"").arg(dataName));
    if (dataName.isEmpty())
    {
        logOperationTrace("plot data request aborted: empty name");
        return;
    }

    const std::string yColName = dataName.toStdString();

    auto& dm = m_viewer.GetDataManager();
    auto& pm = m_viewer.GetPlotManager();

    // 验证列存在
    const size_t yIdx = dm.GetColumnIndex(yColName);
    if (yIdx == static_cast<size_t>(-1))
    {
        logOperationTrace(QString("plot data request aborted: column not found name=\"%1\"").arg(dataName));
        return;
    }

    // 确保有激活页面（无激活页时自动创建）
    if (!pm.hasActivePage())
    {
        logOperationTrace("plot data request creating first page");
        pm.addPage();
    }

    // ---- X 轴自动/手动设置 ----
    const int pageIdx = pm.activePageIndex();
    size_t xIdx = pm.xAxisColumn(pageIdx);
    if (xIdx == static_cast<size_t>(-1))
    {
        // 1. 尝试使用全局检测结果
        const size_t globalX = dm.GetXAxisColumn();
        if (globalX != static_cast<size_t>(-1))
        {
            logXAxisTrace(QString("plot data auto X-axis page=%1 column=%2").arg(pageIdx).arg(globalX));
            pm.setXAxisColumn(pageIdx, globalX);
        }
        else
        {
            // 2. 全局也没检测到 -> 弹出对话框让用户选择
            const auto& colNames = dm.GetColumnNames();
            QStringList items;
            items << QString::fromUtf8("[数据索引]");
            for (const auto& name : colNames)
                items << QString::fromStdString(name);

            bool ok = false;
            const QString choice = QInputDialog::getItem(
                this,
                QString::fromUtf8("选择 X 轴"),
                QString::fromUtf8("未检测到默认 X 轴变量，请手动选择："),
                items, 0, false, &ok);

            if (!ok || choice.isEmpty())
            {
                logXAxisTrace(QString("plot data X-axis selection cancelled page=%1").arg(pageIdx));
                return;
            }

            if (choice == QString::fromUtf8("[数据索引]"))
            {
                pm.setXAxisColumn(pageIdx, static_cast<size_t>(-1));
                logXAxisTrace(QString("plot data X-axis set to index page=%1").arg(pageIdx));
            }
            else
            {
                const size_t selIdx = dm.GetColumnIndex(choice.toStdString());
                pm.setXAxisColumn(pageIdx, selIdx);
                logXAxisTrace(QString("plot data X-axis selected page=%1 column=%2 name=\"%3\"")
                              .arg(pageIdx).arg(selIdx).arg(choice));
            }
        }
    }

    // 如果当前 Y 列就是当前图窗的 X 轴列，则跳过
    {
        const size_t pxIdx = pm.xAxisColumn(pageIdx);
        if (pxIdx != static_cast<size_t>(-1) && pxIdx < dm.GetColumnNames().size())
        {
            const auto& colNames = dm.GetColumnNames();
            if (yColName == colNames[pxIdx])
            {
                logOperationTrace(QString("plot data request skipped: requested Y is page X-axis page=%1 name=\"%2\"")
                                  .arg(pageIdx).arg(dataName));
                return;
            }
        }
    }

    // FFT 图窗禁止添加数据项
    if (pm.hasActivePage() && pm.isFFTPage(pm.activePageIndex()))
    {
        logOperationTrace(QString("plot data request blocked on FFT page=%1").arg(pm.activePageIndex()));
        return;
    }

    const bool added = pm.addDataToActivePage(yColName);
    logOperationTrace(QString("plot data request leave page=%1 name=\"%2\" added=%3")
                      .arg(pm.activePageIndex()).arg(dataName).arg(added));
}

void UI::onDataItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;

    const QVariant itemKind = item->data(0, kDataTreeKindRole);
    if (itemKind.isValid() && itemKind.toInt() != kDataTreeLeafKind)
        return;

    const QString dataName = item->data(0, kDataTreeNameRole).toString().isEmpty()
        ? item->text(0)
        : item->data(0, kDataTreeNameRole).toString();
    plotDataColumnByName(dataName);
}
