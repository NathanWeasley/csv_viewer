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
#include <qtimer.h>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
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
QString shutdownTracePath()
{
    const QString userDir = QCoreApplication::applicationDirPath() + "/user";
    QDir().mkpath(userDir);
    return userDir + "/shutdown_trace.txt";
}

QString plotTracePath()
{
    const QString userDir = QCoreApplication::applicationDirPath() + "/user";
    QDir().mkpath(userDir);
    return userDir + "/plot_gl_trace.txt";
}
}

UI::UI(QWidget *parent)
    : QMainWindow(parent)
{
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
}

UI::~UI()
{
    logShutdownTrace("~UI enter");
    beginShutdownCleanup(true);
    logShutdownTrace("~UI leave");
}

void UI::init()
{
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
    createMain();

    // ---- 加载 X 轴规则 ----
    {
        QString jsonPath = QCoreApplication::applicationDirPath() + "/user/xaxis.json";
        m_viewer.GetDataManager().LoadXAxisRules(jsonPath.toStdString());
    }

    // ---- 加载变量自动重命名规则 ----
    loadAliasFile();

    // ---- 加载书签 ----
    loadBookmarkFile();

    createToolbar();
    createStatusbar();

    bindCursorManagerCallbacks();
}

void UI::saveState()
{
    auto& sm = m_viewer.GetStyleManager();
    QString stylePath = QCoreApplication::applicationDirPath() + "/user/style.json";
    sm.save(stylePath.toStdString());
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

        saveState();
        logShutdownTrace("beginShutdownCleanup saved UI state");
    }

    disconnectViewerCallbacks();
    logShutdownTrace("beginShutdownCleanup disconnected viewer callbacks");
    cleanupPlotOverlaysBeforeShutdown();
    logShutdownTrace("beginShutdownCleanup finished overlay cleanup");
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
    QFile file(shutdownTracePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " | " << message << "\n";
    out.flush();
}

void UI::logPlotTrace(const QString& message) const
{
    QFile file(plotTracePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " | " << message << "\n";
    out.flush();
}

void UI::configurePlotDrawingMode(QCustomPlot* plot, bool enabled) const
{
    if (!plot)
        return;

    // CPU 模式恢复 QCustomPlot 原始的软件绘制路径。
    // OpenGL 模式统一交给 QCustomPlot 的 OpenGL 后端，不再单独拆分散点路径。
    if (auto* overlayLayer = plot->layer("overlay"))
        overlayLayer->setMode(enabled ? QCPLayer::lmLogical : QCPLayer::lmBuffered);

    plot->setOpenGl(enabled, 0);

    logPlotTrace(QString("configure drawing mode page=%1 openGl=%2 overlayMode=%3")
                 .arg(m_plotToPageIndex.value(plot, -1))
                 .arg(plot->openGl())
                 .arg(plot->layer("overlay") && plot->layer("overlay")->mode() == QCPLayer::lmLogical ? "logical" : "buffered"));
}

void UI::applyOpenGlDrawingMode(bool enabled)
{
    const auto pageIndices = m_pageDocks.keys();
    for (int pageIndex : pageIndices)
    {
        auto* plot = getPlot(pageIndex);
        if (!plot)
            continue;

        const bool prevUpdatesEnabled = plot->updatesEnabled();
        plot->setUpdatesEnabled(false);
        configurePlotDrawingMode(plot, enabled);
        plot->setUpdatesEnabled(prevUpdatesEnabled);

        if (prevUpdatesEnabled)
            plot->replot(QCustomPlot::rpQueuedRefresh);
    }
}
// ============================================================
// 双击数据树项 → 添加到激活图窗
// ============================================================

void UI::onDataItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;

    std::string yColName = item->text(0).toStdString();

    auto& dm = m_viewer.GetDataManager();
    auto& pm = m_viewer.GetPlotManager();

    // 验证列存在
    size_t yIdx = dm.GetColumnIndex(yColName);
    if (yIdx == static_cast<size_t>(-1))
        return;

    // 确保有激活页面（无激活页时自动创建）
    if (!pm.hasActivePage())
        pm.addPage();

    // ---- X 轴自动/手动设置 ----
    int pageIdx = pm.activePageIndex();
    size_t xIdx = pm.xAxisColumn(pageIdx);
    if (xIdx == static_cast<size_t>(-1))
    {
        // 1. 尝试使用全局检测结果
        size_t globalX = dm.GetXAxisColumn();
        if (globalX != static_cast<size_t>(-1))
        {
            pm.setXAxisColumn(pageIdx, globalX);
        }
        else
        {
            // 2. 全局也没检测到 → 弹出对话框让用户选择
            const auto& colNames = dm.GetColumnNames();
            QStringList items;
            items << QString::fromUtf8("[数据索引]");
            for (const auto& name : colNames)
                items << QString::fromStdString(name);

            bool ok = false;
            QString choice = QInputDialog::getItem(
                this,
                QString::fromUtf8("选择 X 轴"),
                QString::fromUtf8("未检测到默认 X 轴变量，请手动选择："),
                items, 0, false, &ok);

            if (!ok || choice.isEmpty())
                return;  // 用户取消

            if (choice == QString::fromUtf8("[数据索引]"))
            {
                pm.setXAxisColumn(pageIdx, static_cast<size_t>(-1));
            }
            else
            {
                size_t selIdx = dm.GetColumnIndex(choice.toStdString());
                pm.setXAxisColumn(pageIdx, selIdx);
            }
        }
    }

    // 检查：如果 yColName 是当前图窗的 X 轴列，跳过（不添加到 Y 轴）
    {
        size_t pxIdx = pm.xAxisColumn(pageIdx);
        if (pxIdx != static_cast<size_t>(-1) && pxIdx < dm.GetColumnNames().size())
        {
            const auto& colNames = dm.GetColumnNames();
            if (yColName == colNames[pxIdx])
                return;
        }
    }

    // FFT 图窗禁止添加数据项
    if (pm.hasActivePage() && pm.isFFTPage(pm.activePageIndex()))
        return;

    // 向 PlotManager 注册（自动处理去重）
    pm.addDataToActivePage(yColName);
}
