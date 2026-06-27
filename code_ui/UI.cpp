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

static bool isSystemInDark()
{
    Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();

    return (scheme == Qt::ColorScheme::Dark);
}

UI::UI(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_dockManager = new ads::CDockManager(ui.centralWidget);

    init();

    /** Restore window settings */
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
        }
    }
}

UI::~UI()
{
    saveState();
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

void UI::createMenu()
{
    /** Generating MainWindow part */

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openCSV = fileMenu->addAction("Open CSV file");
    auto* openFolder = fileMenu->addAction("Open CSV files in folders");
    fileMenu->addSeparator();
    auto* clearAll = fileMenu->addAction("Clear loaded data");

    auto* settingsMenu = menuBar()->addMenu("&Settings");
    auto* aliasAction = settingsMenu->addAction("Auto Rename");
    connect(aliasAction, &QAction::triggered, this, &UI::showAliasDialog);
}

void UI::createToolbar()
{
    ///< Load icons and create buttons
    QIcon loadcsv = createDpiAwareIcon(g_iconBase64[ENUM2IDX(IconIdx::LOADCSV)]);
    auto* action_loadcsv = new QAction(loadcsv, "Load CSVs", this);

    QIcon loadfolder = createDpiAwareIcon(g_iconBase64[ENUM2IDX(IconIdx::LOADFOLDER)]);
    auto* action_loadfolder = new QAction(loadfolder, "Load Folders", this);

    QIcon clearall = createDpiAwareIcon(g_iconBase64[ENUM2IDX(IconIdx::CLEAR)]);
    auto* action_clearall = new QAction(clearall, "Clear All", this);

    QIcon dofft = createDpiAwareIcon(g_iconBase64[ENUM2IDX(IconIdx::FFT)]);
    auto* action_dofft = new QAction(dofft, "FFT", this);

    QIcon addfx = createDpiAwareIcon(g_iconBase64[ENUM2IDX(IconIdx::EXPR)]);
    auto* action_addfx = new QAction(addfx, "Expression", this);

    ///< Add buttons to toolbar
    ui.mainToolBar->addAction(action_loadcsv);
    ui.mainToolBar->addAction(action_loadfolder);
    ui.mainToolBar->addAction(action_clearall);
    ui.mainToolBar->addSeparator();
    ui.mainToolBar->addAction(action_dofft);
    ui.mainToolBar->addAction(action_addfx);

    ///< Connect buttons to slots
    connect(action_loadcsv, &QAction::triggered, this, &UI::onLoadCSVClicked);
    connect(action_clearall, &QAction::triggered, this, [this]()
    {
        m_viewer.Clear();
        m_dataTree->clear();
        m_xAxisLabel->setText("X: (none)");
    });
}

void UI::createMain()
{
    ///< Center plot area — QTabWidget for multiple plot pages
    m_plotTabs = new QTabWidget();
    m_plotTabs->setTabsClosable(true);
    m_plotTabs->setMovable(true);
    connect(m_plotTabs, &QTabWidget::currentChanged, this,
        [this](int index)
        {
            m_viewer.GetPlotManager().setActivePage(index);
        });
    connect(m_plotTabs, &QTabWidget::tabCloseRequested, this,
        [this](int index)
        {
            m_viewer.GetPlotManager().removePage(index);
        });

    m_plotDock = new ads::CDockWidget("Plot");
    m_plotDock->setWidget(m_plotTabs);
    m_plotDock->setFeatures(ads::CDockWidget::DockWidgetDeleteOnClose);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_plotDock);

    ///< Left DateTree
    m_dataTree = new QTreeWidget();
    m_dataTree->setHeaderHidden(true);
    m_dataTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dataTree, &QTreeWidget::customContextMenuRequested, this,
        [this](const QPoint& /*pos*/)
        {
            QTreeWidgetItem* item = m_dataTree->currentItem();
            if (!item)
                return;

            QMenu menu;
            QAction* copyAction = menu.addAction("Copy Data Name");
            connect(copyAction, &QAction::triggered, this, [item]()
            {
                QApplication::clipboard()->setText(item->text(0));
            });
            menu.exec(QCursor::pos());
        });
    connect(m_dataTree, &QTreeWidget::itemDoubleClicked, this, &UI::onDataItemDoubleClicked);

    m_dataDock = new ads::CDockWidget("Data");
    m_dataDock->setWidget(m_dataTree);
    m_dataDock->setFeatures(ads::CDockWidget::DockWidgetDeleteOnClose);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_dataDock, m_plotDock->dockAreaWidget());

    ///< Right Bookmark Tree
    m_bookmarkTree = new QTreeWidget();
    m_bookmarkTree->setHeaderHidden(true);
    m_bookmarkDock = new ads::CDockWidget("Bookmarks");
    m_bookmarkDock->setWidget(m_bookmarkTree);
    m_bookmarkDock->setFeatures(ads::CDockWidget::DockWidgetDeleteOnClose);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_bookmarkDock, m_plotDock->dockAreaWidget());

    ///< Bind PlotManager callbacks
    bindPlotManagerCallbacks();
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
    connect(&m_viewer, &viewer::Viewer::LoadStarted, this, [this](int /*totalFiles*/)
    {
        m_progressBar->setValue(0);
        m_progressBar->show();
    });

    connect(&m_viewer, &viewer::Viewer::BusyProgressChanged, this, [this](float globalProgress)
    {
        m_progressBar->setValue(static_cast<int>(globalProgress * 1000.0f));
    });

    connect(&m_viewer, &viewer::Viewer::LoadFinished, this, [this]()
    {
        m_progressBar->setValue(1000);
        m_progressBar->hide();

        // Populate the Data tree with column names
        m_dataTree->clear();
        const auto& colNames = m_viewer.GetDataManager().GetColumnNames();
        for (const auto& name : colNames)
        {
            auto* item = new QTreeWidgetItem(m_dataTree);
            item->setText(0, QString::fromStdString(name));
        }

        // Update X-axis label
        if (m_viewer.GetDataManager().GetColumnCount() > 0)
        {
            size_t xIdx = m_viewer.GetDataManager().GetXAxisColumn();
            if (xIdx < colNames.size())
                m_xAxisLabel->setText(QString("X: %1").arg(QString::fromStdString(colNames[xIdx])));
        }
    });

    // Handle cross-file column validation errors
    connect(&m_viewer, &viewer::Viewer::LoadError, this, [this](const QString& message)
    {
        m_progressBar->setValue(0);
        m_progressBar->hide();
        m_dataTree->clear();
        m_xAxisLabel->setText("X: (none)");
        QMessageBox::critical(this, "CSV Load Error", message);
    });
}

void UI::closeEvent(QCloseEvent* event)
{
    QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
    QSettings settings(configPath, QSettings::IniFormat);

    settings.setValue("geometry", saveGeometry());
    if (m_dockManager)
    {
        settings.setValue("dockingState", m_dockManager->saveState());
    }

    QMainWindow::closeEvent(event);
}

QIcon UI::createDpiAwareIcon(const QString& fullstr, int logicalsize)
{
    // 1. Strip prefix and decode text
    QString base64Data = fullstr.section(',', 1);
    QByteArray svgBytes = QByteArray::fromBase64(base64Data.toUtf8());
    QString svgText = QString::fromUtf8(svgBytes);

    if (isSystemInDark())
    {
        //svgText.replace("#323544", "#7F7F7F", Qt::CaseInsensitive);
        forceStrokeColor(svgText, "#AFAFAF");
    }
    else
    {
        forceStrokeColor(svgText, "#222222");
    }
    svgBytes = svgText.toUtf8();

    // 2. Query the primary screen's current device pixel ratio (e.g., 1.5, 2.0)
    qreal dpr = QGuiApplication::primaryScreen()->devicePixelRatio();

    // 3. Compute actual physical pixel size needed for this display scale
    int physicalSize = qRound(logicalsize * dpr);

    // 4. Render the vector graphic onto a physical-sized canvas
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(svgBytes);
    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();

    // 5. CRITICAL: Inform Qt of the scale factor so it shrinks it down crisply
    pixmap.setDevicePixelRatio(dpr);

    return QIcon(pixmap);
}

void UI::forceStrokeColor(QString& str, const QString& color)
{
    QRegularExpression strokeAttrRegex("stroke=\"[^\"]*\"");
    QString newStrokeAttr = QString("stroke=\"%1\"").arg(color);
    str.replace(strokeAttrRegex, newStrokeAttr);
}

void UI::onLoadCSVClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select CSV files",
        QString(),
        "CSV Files (*.csv);;All Files (*.*)");

    if (files.isEmpty())
        return;

    // Forward the file list to the Viewer's slot for loading
    m_viewer.OnLoadCSV(files);
}

// ============================================================
// PlotManager → Qt 控件绑定
// ============================================================

void UI::bindPlotManagerCallbacks()
{
    auto& pm = m_viewer.GetPlotManager();
    auto& sm = m_viewer.GetStyleManager();

    // ---- 色板下拉填充辅助 (按值捕获 this，避免悬空引用) ----
    auto populateColorCombo = [this](QComboBox* combo, int defaultIndex = 0)
    {
        auto& sm = m_viewer.GetStyleManager();
        combo->blockSignals(true);
        combo->clear();
        size_t n = sm.paletteColorCount();
        for (size_t i = 0; i < n; ++i)
        {
            QColor c = sm.paletteColorAt(i);
            QPixmap swatch(20, 14);
            swatch.fill(c);
            combo->addItem(QIcon(swatch), QString(), QVariant::fromValue(c));
        }
        if (defaultIndex >= 0 && defaultIndex < static_cast<int>(n))
            combo->setCurrentIndex(defaultIndex);
        combo->blockSignals(false);
    };

    // ---- 重建所有页面中工具栏色板下拉的 lambda (按值捕获 populateColorCombo) ----
    auto rebuildAllColorCombos = [this, populateColorCombo]()
    {
        auto& styleMgr = m_viewer.GetStyleManager();
        for (int pi = 0; pi < m_plotTabs->count(); ++pi)
        {
            auto* container = m_plotTabs->widget(pi);
            if (!container) continue;
            auto* vbox = container->findChild<QVBoxLayout*>();
            if (!vbox || vbox->count() < 1) continue;
            auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
            if (!toolbar) continue;
            auto* hb = toolbar->findChild<QHBoxLayout*>();
            if (!hb || hb->count() < 9) continue;

            auto* cmbLC = qobject_cast<QComboBox*>(hb->itemAt(3)->widget());
            auto* cmbSCo = qobject_cast<QComboBox*>(hb->itemAt(6)->widget());

            if (cmbLC)
            {
                QColor prevLC = cmbLC->currentData(Qt::UserRole).value<QColor>();
                populateColorCombo(cmbLC);
                // 尝试匹配之前的颜色
                for (int i = 0; i < cmbLC->count(); ++i)
                {
                    if (cmbLC->itemData(i, Qt::UserRole).value<QColor>() == prevLC)
                    { cmbLC->setCurrentIndex(i); break; }
                }
            }
            if (cmbSCo)
            {
                QColor prevSCo = cmbSCo->currentData(Qt::UserRole).value<QColor>();
                populateColorCombo(cmbSCo);
                for (int i = 0; i < cmbSCo->count(); ++i)
                {
                    if (cmbSCo->itemData(i, Qt::UserRole).value<QColor>() == prevSCo)
                    { cmbSCo->setCurrentIndex(i); break; }
                }
            }
        }
    };

    // 色板切换回调
    sm.onPaletteChanged = [rebuildAllColorCombos]()
    {
        rebuildAllColorCombos();
    };

    // 页面添加 → 创建新 QCustomPlot 页（含工具栏）
    pm.onPageAdded = [this, populateColorCombo](int index)
    {
        // ---- QCustomPlot ----
        auto* plot = new QCustomPlot();
        plot->setOpenGl(true);
        plot->setInteraction(QCP::iRangeDrag, true);
        plot->setInteraction(QCP::iRangeZoom, true);
        plot->xAxis->setLabel("X");
        plot->yAxis->setLabel("Y");

        // 深浅色主题适配（从 StyleManager 读取）
        {
            const auto& theme = m_viewer.GetStyleManager().plotTheme(isSystemInDark());
            QColor bgColor   = theme.bgColor.toQColor();
            QColor axisColor = theme.axisLabelColor.toQColor();
            QColor tickColor = theme.tickLabelColor.toQColor();
            QPen  basePen(theme.basePenColor.toQColor(), theme.basePenWidth);

            plot->setBackground(bgColor);
            plot->xAxis->setLabelColor(axisColor);
            plot->yAxis->setLabelColor(axisColor);
            plot->xAxis->setTickLabelColor(tickColor);
            plot->yAxis->setTickLabelColor(tickColor);
            plot->xAxis->setBasePen(basePen);
            plot->yAxis->setBasePen(basePen);
            plot->xAxis->setTickPen(basePen);
            plot->yAxis->setTickPen(basePen);
            plot->xAxis->setSubTickPen(basePen);
            plot->yAxis->setSubTickPen(basePen);
        }

        // 事件过滤器
        plot->installEventFilter(this);

        // 图例交互
        plot->setInteraction(QCP::iSelectLegend, true);

        // 右键上下文菜单
        plot->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(plot, &QCustomPlot::customContextMenuRequested, this,
            [this, plot, index](const QPoint& pos)
            {
                auto& pm = m_viewer.GetPlotManager();

                QMenu menu;

                QAction* newPageAction = menu.addAction("新建图窗");
                connect(newPageAction, &QAction::triggered, this, [this]()
                {
                    m_viewer.GetPlotManager().addPage();
                });

                QAction* dupPageAction = menu.addAction("复制图窗");
                connect(dupPageAction, &QAction::triggered, this, [this, plot, index]()
                {
                    auto& pm = m_viewer.GetPlotManager();

                    // 在 addPage() 之前复制数据项列表（避免 vector 扩容使引用失效）
                    const auto& srcDataItems = pm.pageInfo(index).dataItems;
                    std::vector<std::string> itemsToCopy(srcDataItems.begin(), srcDataItems.end());
                    bool legendOn = pm.pageInfo(index).legendVisible;

                    // 创建新图窗
                    int newIdx = pm.addPage();

                    // ---- 复制 X 轴配置 ----
                    pm.setXAxisColumn(newIdx, pm.pageInfo(index).xAxisColumn);

                    // ---- 先复制表达式数据（在 addDataItem 之前，避免 getOrCreate 创建的本地拷贝被替换）----
                    {
                        auto& srcExprMgr = pm.pageInfo(index).exprMgr;
                        auto& dstExprMgr = pm.pageInfo(newIdx).exprMgr;
                        dstExprMgr.insertAll(srcExprMgr.copyAll());
                    }

                    // 复制所有数据项（onDataItemAdded 中的 getOrCreate 会发现已有表达式）
                    for (const auto& item : itemsToCopy)
                        pm.addDataItem(newIdx, item);

                    // 复制图例状态
                    if (legendOn)
                        pm.setLegendVisible(newIdx, true);

                    // ---- 复制高亮规则 ----
                    {
                        auto& srcHL = pm.pageInfo(index).highlightMgr;
                        auto& dstHL = pm.pageInfo(newIdx).highlightMgr;
                        dstHL.insertAllRules(srcHL.copyAllRules());
                    }

                    // ---- 复制样式：从源 plot 的 graph 复制 pen + scatter 到目标 plot ----
                    auto* dstContainer = m_plotTabs->widget(newIdx);
                    auto* dstPlot = dstContainer ? dstContainer->findChild<QCustomPlot*>() : nullptr;
                    if (dstPlot)
                    {
                        for (int si = 0; si < plot->plottableCount(); ++si)
                        {
                            auto* srcGraph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(si));
                            if (!srcGraph) continue;

                            std::string gname = srcGraph->name().toStdString();

                            viewer::QCPColumnGraph* dstGraph = nullptr;
                            for (int di = 0; di < dstPlot->plottableCount(); ++di)
                            {
                                auto* dg = dynamic_cast<viewer::QCPColumnGraph*>(dstPlot->plottable(di));
                                if (dg && dg->name().toStdString() == gname)
                                {
                                    dstGraph = dg;
                                    break;
                                }
                            }
                            if (!dstGraph) continue;

                            dstGraph->setPen(srcGraph->pen());
                            dstGraph->setScatterStyle(srcGraph->scatterStyle());
                        }
                        // 渲染新图窗的高亮
                        renderHighlights(newIdx);

                        // ---- 复制游标 ----
                        {
                            auto& cm = m_viewer.GetCursorManager();
                            const auto& cursors = cm.cursors();
                            for (size_t ci = 0; ci < cursors.size(); ++ci)
                            {
                                if (cursors[ci].pageIndex == index)
                                    cm.addCursor(newIdx, cursors[ci].dataItemName, cursors[ci].dataIndex);
                            }
                        }

                        dstPlot->replot();
                    }
                });

                menu.addSeparator();

                QAction* rescaleAction = menu.addAction("还原缩放");
                connect(rescaleAction, &QAction::triggered, this, [this, index]()
                {
                    if (auto& cb = m_viewer.GetPlotManager().onRescaleRequested)
                        cb(index);
                });

                QAction* rectZoomAction = menu.addAction("框选缩放");
                rectZoomAction->setCheckable(true);
                rectZoomAction->setChecked(pm.isRectZoomActive(index));
                connect(rectZoomAction, &QAction::toggled, this, [this, index](bool checked)
                {
                    m_viewer.GetPlotManager().setRectZoomActive(index, checked);
                });

                menu.addSeparator();

                QAction* legendAction = menu.addAction("显示图例");
                legendAction->setCheckable(true);
                legendAction->setChecked(pm.isLegendVisible(index));
                connect(legendAction, &QAction::toggled, this, [this, index](bool checked)
                {
                    m_viewer.GetPlotManager().setLegendVisible(index, checked);
                });

                menu.addSeparator();

                QAction* highlightAction = menu.addAction("高亮规则");
                connect(highlightAction, &QAction::triggered, this, [this, index]()
                {
                    showHighlightDialog(index);
                });

                menu.addSeparator();

                QAction* exportAction = menu.addAction("导出图片");
                connect(exportAction, &QAction::triggered, this, [this, index]()
                {
                    exportPlotImage(index);
                });
                menu.addAction("加入收藏夹");

                menu.exec(plot->mapToGlobal(pos));
            });

        // ---- 工具栏 ----
        auto* toolbar = new QWidget();
        toolbar->setFixedHeight(32);
        auto* hbox = new QHBoxLayout(toolbar);
        hbox->setContentsMargins(2, 2, 2, 2);
        hbox->setSpacing(4);

        // 1. 下拉列表：当前图窗全部数据项
        auto* cmbDataItem = new QComboBox();
        cmbDataItem->setMinimumWidth(100);
        cmbDataItem->setMaximumWidth(160);
        cmbDataItem->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        hbox->addWidget(cmbDataItem);

        // 2. 线形下拉
        auto* cmbLineStyle = new QComboBox();
        cmbLineStyle->addItems({"实线", "点线", "虚线", "点划线"});
        hbox->addWidget(cmbLineStyle);

        // 3. 线宽数字框
        auto* spnLineWidth = new QSpinBox();
        spnLineWidth->setRange(1, 20);
        spnLineWidth->setValue(1);
        hbox->addWidget(spnLineWidth);

        // 4. 线色下拉（色块+图标）
        auto* cmbLineColor = new QComboBox();
        populateColorCombo(cmbLineColor, 8);
        hbox->addWidget(cmbLineColor);

        // 5. 数据点类型下拉
        auto* cmbScatter = new QComboBox();
        cmbScatter->addItems({"无", "空心圆", "实心圆", "方形", "菱形", "星号"});
        hbox->addWidget(cmbScatter);

        // 6. 数据点大小
        auto* spnScatterSize = new QSpinBox();
        spnScatterSize->setRange(0, 50);
        spnScatterSize->setValue(0);
        hbox->addWidget(spnScatterSize);

        // 7. 数据点颜色（色块+图标）
        auto* cmbScatterColor = new QComboBox();
        populateColorCombo(cmbScatterColor, 8);
        hbox->addWidget(cmbScatterColor);

        hbox->addStretch();

        // 8. 删除按钮（最右侧）
        auto* btnDelete = new QPushButton("✕");
        btnDelete->setFixedSize(24, 24);
        btnDelete->setEnabled(false);
        btnDelete->setToolTip("删除当前选中的数据曲线");
        hbox->addWidget(btnDelete);

        // ---- 表达式编辑栏 ----
        auto* exprBar = new QWidget();
        exprBar->setFixedHeight(42);
        auto* exprHBox = new QHBoxLayout(exprBar);
        exprHBox->setContentsMargins(6, 3, 6, 3);
        exprHBox->setSpacing(4);

        auto* fxLabel = new QLabel("fx=");
        fxLabel->setStyleSheet("color: #888; font-weight: bold;");
        exprHBox->addWidget(fxLabel);

        auto* exprLineEdit = new QLineEdit();
        exprLineEdit->setPlaceholderText("expression...");
        exprLineEdit->setStyleSheet(
            "QLineEdit { border: 1px solid #555; border-radius: 3px; padding: 2px 6px; "
            "background: #2a2a2a; color: #ddd; }"
            "QLineEdit:focus { border-color: #FFD700; }");
        exprHBox->addWidget(exprLineEdit, 1);

        // ---- 容器 ----
        auto* container = new QWidget();
        auto* vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        vbox->addWidget(toolbar);
        vbox->addWidget(plot, 1); // plot 占剩余空间
        vbox->addWidget(exprBar);

        // ---- ComboList 选择变化 → 加载样式 + 更新选中状态 ----
        connect(cmbDataItem, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, plot, index, cmbDataItem](int /*idx*/)
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string yColName = nameData.toStdString();
                auto& pm = m_viewer.GetPlotManager();
                pm.setSelectedDataItem(index, yColName);
            });

        // ---- 删除按钮 → 从图窗移除数据曲线 ----
        connect(btnDelete, &QPushButton::clicked, this,
            [this, index, cmbDataItem]()
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string selName = nameData.toStdString();
                auto& pm = m_viewer.GetPlotManager();
                pm.removeDataItem(index, selName);
            });

        // ---- 工具栏控件→graph 回写辅助 ----
        auto applyToGraph = [this, plot, cmbDataItem]()
        {
            QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
            if (nameData.isEmpty()) return;
            std::string selName = nameData.toStdString();

            viewer::QCPColumnGraph* graph = nullptr;
            for (int i = 0; i < plot->plottableCount(); ++i)
            {
                auto* p = plot->plottable(i);
                if (p && p->name().toStdString() == selName)
                {
                    graph = dynamic_cast<viewer::QCPColumnGraph*>(p);
                    break;
                }
            }
            if (!graph)
                return;

            // 从小部件取新值并应用
            auto* container = qobject_cast<QWidget*>(plot->parent());
            if (!container) return;
            auto* toolbar = container->findChild<QWidget*>();
            if (!toolbar) return;
            auto* hb = toolbar->findChild<QHBoxLayout*>();
            if (!hb) return;

            auto* cmbLS = qobject_cast<QComboBox*>(hb->itemAt(1)->widget());
            auto* spnLW = qobject_cast<QSpinBox*>(hb->itemAt(2)->widget());
            auto* cmbLC = qobject_cast<QComboBox*>(hb->itemAt(3)->widget());
            auto* cmbSC = qobject_cast<QComboBox*>(hb->itemAt(4)->widget());
            auto* spnSS = qobject_cast<QSpinBox*>(hb->itemAt(5)->widget());
            auto* cmbSCo = qobject_cast<QComboBox*>(hb->itemAt(6)->widget());

            QPen pen = graph->pen();
            // 线型
            static const Qt::PenStyle penStyles[] = {Qt::SolidLine, Qt::DotLine, Qt::DashLine, Qt::DashDotLine};
            pen.setStyle(penStyles[cmbLS->currentIndex()]);
            // 线宽
            pen.setWidth(spnLW->value());
            // 线色（从 UserRole 取 QColor）
            QColor lineColor = cmbLC->currentData(Qt::UserRole).value<QColor>();
            if (lineColor.isValid())
                pen.setColor(lineColor);
            graph->setPen(pen);

            // 散点
            QCPScatterStyle ss = graph->scatterStyle();
            int scIdx = cmbSC->currentIndex();
            static const QCPScatterStyle::ScatterShape shapes[] = {
                QCPScatterStyle::ssNone, QCPScatterStyle::ssCircle, QCPScatterStyle::ssDisc,
                QCPScatterStyle::ssSquare, QCPScatterStyle::ssDiamond, QCPScatterStyle::ssStar
            };
            ss.setShape(shapes[scIdx]);
            ss.setSize(spnSS->value());
            QColor scatterColor = cmbSCo->currentData(Qt::UserRole).value<QColor>();
            if (scatterColor.isValid())
            {
                QPen spen(scatterColor);
                ss.setPen(spen);
            }
            graph->setScatterStyle(ss);

            plot->replot(); // pen 变化需要手动 replot
        };

        // 连接工具栏控件的变更信号 → applyToGraph
        connect(cmbLineStyle, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [applyToGraph](int){ applyToGraph(); });
        connect(spnLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [applyToGraph](int){ applyToGraph(); });
        connect(cmbLineColor, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [applyToGraph](int){ applyToGraph(); });
        connect(cmbScatter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [applyToGraph](int){ applyToGraph(); });
        connect(spnScatterSize, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [applyToGraph](int){ applyToGraph(); });
        connect(cmbScatterColor, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [applyToGraph](int){ applyToGraph(); });

        // ---- 查找 combo item 索引的辅助 lambda（按 UserRole 匹配） ----
        auto findComboIndexByUserRole = [cmbDataItem](const std::string& name) -> int {
            QString qName = QString::fromStdString(name);
            for (int i = 0; i < cmbDataItem->count(); ++i) {
                if (cmbDataItem->itemData(i, Qt::UserRole).toString() == qName)
                    return i;
            }
            return -1;
        };

        // ---- 存储 widget 引用 ----
        m_exprLineEdits[index] = exprLineEdit;
        m_toolbarCombos[index] = cmbDataItem;

        // ---- 表达式编辑框 textChanged → 合法性检查 + 计算 ----
        connect(exprLineEdit, &QLineEdit::textChanged, this,
            [this, plot, index, cmbDataItem, exprLineEdit, findComboIndexByUserRole](const QString& text)
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string selName = nameData.toStdString();

                auto& dm = m_viewer.GetDataManager();
                auto& exprMgr = m_viewer.GetPlotManager().pageInfo(index).exprMgr;

                // 更新表达式文本
                exprMgr.setExpressionText(selName, text.toStdString());

                // 用 exprtk 检查合法性
                std::string exprStr = text.toStdString();
                if (exprStr.empty())
                {
                    exprLineEdit->setStyleSheet(
                        "QLineEdit { border: 1px solid #555; border-radius: 3px; padding: 2px 6px; "
                        "background: #2a2a2a; color: #ddd; }"
                        "QLineEdit:focus { border-color: #FFD700; }");
                    return;
                }

                if (!exprMgr.validate(exprStr, dm))
                {
                    // 不合法：红色边框提示
                    exprLineEdit->setStyleSheet(
                        "QLineEdit { border: 1px solid #cc3333; border-radius: 3px; padding: 2px 6px; "
                        "background: #2a2a2a; color: #ddd; }"
                        "QLineEdit:focus { border-color: #ff4444; }");
                    return;
                }

                // 合法：恢复正常边框 + 计算
                exprLineEdit->setStyleSheet(
                    "QLineEdit { border: 1px solid #555; border-radius: 3px; padding: 2px 6px; "
                    "background: #2a2a2a; color: #ddd; }"
                    "QLineEdit:focus { border-color: #FFD700; }");

                // 重新计算表达式值
                if (exprMgr.recompute(selName, dm))
                {
                    // 更新 graph 数据并刷新图窗
                    viewer::QCPColumnGraph* graph = nullptr;
                    for (int i = 0; i < plot->plottableCount(); ++i)
                    {
                        auto* p = plot->plottable(i);
                        if (p && p->name().toStdString() == selName)
                        {
                            graph = dynamic_cast<viewer::QCPColumnGraph*>(p);
                            break;
                        }
                    }
                    if (graph)
                    {
                        // 重新绑定到更新后的本地数据拷贝
                        viewer::PlotExpression* pe = exprMgr.get(selName);
                        if (pe && pe->computedData)
                        {
                            size_t xIdx = m_viewer.GetPlotManager().xAxisColumn(index);
                            const viewer::Column* xCol = (xIdx != static_cast<size_t>(-1))
                                ? dm.GetColumn(xIdx) : dm.GetIndexColumn();
                            graph->setDataColumns(xCol, pe->computedData.get());
                            graph->notifyDataChanged();
                            plot->rescaleAxes(true); // 仅扩大不放缩，保持用户当前视图
                            plot->replot();
                        }
                    }

                    // 更新工具栏星号显示
                    viewer::PlotExpression* pe = exprMgr.get(selName);
                    if (pe)
                    {
                        QString displayName = QString::fromStdString(selName);
                        if (pe->isEdited)
                            displayName += "*";
                        int idx = findComboIndexByUserRole(selName);
                        if (idx >= 0)
                            cmbDataItem->setItemText(idx, displayName);
                    }
                }
            });

        QString title = QString::fromStdString(
            m_viewer.GetPlotManager().pageInfo(index).title);
        m_plotTabs->insertTab(index, container, title);
        m_plotTabs->setCurrentIndex(index);
    };

    // 页面即将移除
    pm.onPageAboutToRemove = [this](int index)
    {
        if (index >= 0 && index < m_plotTabs->count())
        {
            QWidget* w = m_plotTabs->widget(index);
            m_plotTabs->removeTab(index);
            delete w;
        }
    };

    // 页面移除后
    pm.onPageRemoved = [this](int activeIdx, int /*remainingCount*/)
    {
        if (activeIdx >= 0 && activeIdx < m_plotTabs->count())
            m_plotTabs->setCurrentIndex(activeIdx);
    };

    // 激活页面变更
    pm.onActivePageChanged = [this](int index)
    {
        if (index >= 0 && index < m_plotTabs->count()
            && m_plotTabs->currentIndex() != index)
        {
            m_plotTabs->setCurrentIndex(index);
        }

        // 更新状态栏 X 轴标签
        size_t xIdx = m_viewer.GetPlotManager().xAxisColumn(index);
        const auto& colNames = m_viewer.GetDataManager().GetColumnNames();
        if (xIdx != static_cast<size_t>(-1) && xIdx < colNames.size())
            m_xAxisLabel->setText(QString("X: %1").arg(QString::fromStdString(colNames[xIdx])));
        else
            m_xAxisLabel->setText("X: (none)");
    };

    // X 轴变更 → 更新状态栏 + 重新绑定所有 graph 的 X 列
    pm.onXAxisChanged = [this](int pageIndex, size_t colIdx)
    {
        // 更新状态栏标签
        const auto& colNames = m_viewer.GetDataManager().GetColumnNames();
        if (m_viewer.GetPlotManager().activePageIndex() != pageIndex)
            return;
        if (colIdx != static_cast<size_t>(-1) && colIdx < colNames.size())
            m_xAxisLabel->setText(QString("X: %1").arg(QString::fromStdString(colNames[colIdx])));
        else
            m_xAxisLabel->setText("X: (none)");

        // 重新绑定当前图窗所有 graph 的 X 列数据
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;
        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;
        auto& dm = m_viewer.GetDataManager();
        const viewer::Column* newXCol = nullptr;
        if (colIdx != static_cast<size_t>(-1))
            newXCol = dm.GetColumn(colIdx);
        else
        {
            dm.ensureIndexColumnBuilt();
            newXCol = dm.GetIndexColumn();
        }
        if (!newXCol)
            return;
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
            if (graph)
            {
                // 保持原有 Y 列不变，只换 X 列
                const viewer::Column* yCol = nullptr;
                // 通过 graph name 查找 Y 列
                auto& exprMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr;
                viewer::PlotExpression* pe = exprMgr.get(graph->name().toStdString());
                if (pe && pe->computedData)
                    yCol = pe->computedData.get();
                else
                    yCol = dm.GetColumn(graph->name().toStdString());
                if (yCol)
                {
                    graph->setDataColumns(newXCol, yCol);
                    graph->notifyDataChanged();
                }
            }
        }
        plot->rescaleAxes();
        plot->replot();
    };

    // 数据项添加 → 创建 QCPColumnGraph 并绑定到对应 QCustomPlot
    pm.onDataItemAdded = [this](int pageIndex, const std::string& yColName)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        auto& dm = m_viewer.GetDataManager();
        auto& pm = m_viewer.GetPlotManager();
        size_t xIdx = pm.xAxisColumn(pageIndex);

        const viewer::Column* xCol = nullptr;
        const viewer::Column* yCol = dm.GetColumn(yColName);
        if (!yCol)
            return;

        size_t plotCount = m_viewer.GetPlotManager().pageInfo(pageIndex).dataItems.size();

        // 抑制中途重绘，所有 setup 完成后统一 replot
        plot->setUpdatesEnabled(false);

        // 创建 QCPColumnGraph（首次绑定原始 Y 列）
        auto* graph = new viewer::QCPColumnGraph(plot->xAxis, plot->yAxis);

        if (xIdx == static_cast<size_t>(-1))
        {
            dm.ensureIndexColumnBuilt();
            xCol = dm.GetIndexColumn();
            graph->setDataColumns(xCol, yCol);
        }
        else
        {
            xCol = dm.GetColumn(xIdx);
            if (!xCol)
            {
                plot->setUpdatesEnabled(true);
                return;
            }
            graph->setDataColumns(xCol, yCol);
        }

        // 设置默认外观
        QPen pen(graph->pen());
        if (plotCount > 0)
        {
            pen.setColor(m_viewer.GetStyleManager().paletteColorAt(
                (plotCount - 1) % m_viewer.GetStyleManager().paletteColorCount()));
        }
        graph->setPen(pen);
        graph->setName(QString::fromStdString(yColName));

        // 创建表达式本地拷贝，切换到独立数据源
        auto& exprMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr;
        viewer::PlotExpression& pe = exprMgr.getOrCreate(yColName, dm);
        graph->setDataColumns(xCol, pe.computedData.get());
        graph->notifyDataChanged();

        // 首个数据项全量缩放，后续项仅扩大
        graph->rescaleAxes(plotCount > 1);

        // 所有 setup 完成，统一重绘
        plot->setUpdatesEnabled(true);
        plot->replot();

        // 向工具栏 ComboList 添加数据项名称
        auto* vbox = container->findChild<QVBoxLayout*>();
        if (!vbox || vbox->count() < 1)
            return;
        auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
        if (!toolbar)
            return;
        auto* hb = toolbar->findChild<QHBoxLayout*>();
        if (!hb || hb->count() < 9)
            return;
        auto* cmbDataItem = qobject_cast<QComboBox*>(hb->itemAt(0)->widget());
        if (!cmbDataItem)
            return;

        cmbDataItem->blockSignals(true);
        cmbDataItem->addItem(QString::fromStdString(yColName));
        // 在 UserRole 中存储原始数据名（不受 star 装饰影响）
        cmbDataItem->setItemData(cmbDataItem->count() - 1,
            QString::fromStdString(yColName), Qt::UserRole);
        // 如果是第一个数据项，自动设为选中
        if (cmbDataItem->count() == 1)
        {
            cmbDataItem->setCurrentIndex(0);
        }
        cmbDataItem->blockSignals(false);

        // 启用删除按钮
        auto* btnDeleteAdd = qobject_cast<QPushButton*>(hb->itemAt(8)->widget());
        if (btnDeleteAdd)
            btnDeleteAdd->setEnabled(true);

        // 触发初始样式加载
        if (cmbDataItem->count() == 1)
        {
            auto& pm = m_viewer.GetPlotManager();
            pm.setSelectedDataItem(pageIndex, yColName);
        }
    };

    // 数据项移除
    pm.onDataItemRemoved = [this](int pageIndex, const std::string& yColName)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        // ---- 清理属于该数据项的游标（必须在清理表达式之前，避免 replot 访问已释放的数据） ----
        {
            auto& cm = m_viewer.GetCursorManager();
            const auto& cursors = cm.cursors();
            std::vector<int> toRemove;
            for (size_t i = 0; i < cursors.size(); ++i)
            {
                if (cursors[i].pageIndex == pageIndex && cursors[i].dataItemName == yColName)
                    toRemove.push_back(static_cast<int>(i));
            }
            for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
            {
                cm.removeCursor(*it);
            }
        }

        // ---- 清理表达式 ----
        m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr.removeItem(yColName);

        // 查找并移除对应名称的 QCPColumnGraph
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* plottable = plot->plottable(i);
            if (plottable && plottable->name().toStdString() == yColName)
            {
                plot->removePlottable(plottable);
                break;
            }
        }

        plot->replot();

        // 从工具栏 ComboList 移除对应数据项名称
        auto* vbox = container->findChild<QVBoxLayout*>();
        if (!vbox || vbox->count() < 1)
            return;
        auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
        if (!toolbar)
            return;
        auto* hb = toolbar->findChild<QHBoxLayout*>();
        if (!hb || hb->count() < 9)
            return;
        auto* cmbDataItem = qobject_cast<QComboBox*>(hb->itemAt(0)->widget());
        if (!cmbDataItem)
            return;

        // 按 UserRole 查找 combo item 索引
        int rmIdx = -1;
        {
            QString qName = QString::fromStdString(yColName);
            for (int i = 0; i < cmbDataItem->count(); ++i) {
                if (cmbDataItem->itemData(i, Qt::UserRole).toString() == qName) {
                    rmIdx = i;
                    break;
                }
            }
        }
        if (rmIdx >= 0)
        {
            cmbDataItem->blockSignals(true);

            // 如果移除的恰好是当前选中项，先切换到剩余项的第一项
            if (cmbDataItem->currentIndex() == rmIdx)
            {
                if (cmbDataItem->count() > 1)
                {
                    int newIdx = (rmIdx > 0) ? (rmIdx - 1) : 0;
                    cmbDataItem->setCurrentIndex(newIdx);
                }
            }

            cmbDataItem->removeItem(rmIdx);
            cmbDataItem->blockSignals(false);

            // 没有剩余项时禁用删除按钮
            if (cmbDataItem->count() == 0)
            {
                auto* btnDeleteRm = qobject_cast<QPushButton*>(hb->itemAt(8)->widget());
                if (btnDeleteRm)
                    btnDeleteRm->setEnabled(false);
            }

            // 触发切换后的样式加载
            if (cmbDataItem->count() > 0)
            {
                auto& pm = m_viewer.GetPlotManager();
                pm.setSelectedDataItem(pageIndex, cmbDataItem->currentText().toStdString());
            }
            else
            {
                auto& pm = m_viewer.GetPlotManager();
                pm.setSelectedDataItem(pageIndex, std::string());
            }
        }
    };

    // 图例可见性变更
    pm.onLegendVisibilityChanged = [this](int pageIndex, bool visible)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        plot->legend->setVisible(visible);
        plot->legend->setSelectableParts(QCPLegend::spItems);
        plot->replot();
    };

    // 曲线样式变更（UI 层只需 replot 刷新图例）
    pm.onLegendNeedReplot = [this](int pageIndex)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        plot->replot();
    };

    // 框选缩放状态变更
    pm.onRectZoomStateChanged = [this](int pageIndex, bool active)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        if (active)
        {
            plot->setSelectionRectMode(QCP::srmZoom);
            plot->setCursor(Qt::CrossCursor);
        }
        else
        {
            plot->setSelectionRectMode(QCP::srmNone);
            plot->setCursor(Qt::ArrowCursor);
        }
    };

    // 还原缩放请求
    pm.onRescaleRequested = [this](int pageIndex)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        plot->rescaleAxes();
        plot->replot();
    };

    // 选中数据项变更 → 刷新工具栏控件
    pm.onSelectedDataItemChanged = [this](int pageIndex, const std::string& yColName)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        if (!container)
            return;

        // 在 container 的子控件中找到 QCustomPlot
        auto* plot = container->findChild<QCustomPlot*>();
        if (!plot)
            return;

        // 工具栏是 container 布局中的第一个子 widget
        auto* vbox = container->findChild<QVBoxLayout*>();
        if (!vbox || vbox->count() < 1)
            return;

        auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
        if (!toolbar)
            return;

    auto* hb = toolbar->findChild<QHBoxLayout*>();
        if (!hb || hb->count() < 9)
            return;

        auto* cmbDataItem = qobject_cast<QComboBox*>(hb->itemAt(0)->widget());
        auto* cmbLS       = qobject_cast<QComboBox*>(hb->itemAt(1)->widget());
        auto* spnLW       = qobject_cast<QSpinBox*>(hb->itemAt(2)->widget());
        auto* cmbLC       = qobject_cast<QComboBox*>(hb->itemAt(3)->widget());
        auto* cmbSC       = qobject_cast<QComboBox*>(hb->itemAt(4)->widget());
        auto* spnSS       = qobject_cast<QSpinBox*>(hb->itemAt(5)->widget());
        auto* cmbSCo      = qobject_cast<QComboBox*>(hb->itemAt(6)->widget());
        auto* btnDelete   = qobject_cast<QPushButton*>(hb->itemAt(8)->widget());

        // 根据选中状态启用/禁用删除按钮
        if (btnDelete)
            btnDelete->setEnabled(!yColName.empty());

        // ---- 查找 combo item 索引辅助（按 UserRole 匹配）----
        auto findCmbIdx = [cmbDataItem](const std::string& name) -> int {
            QString qName = QString::fromStdString(name);
            for (int i = 0; i < cmbDataItem->count(); ++i) {
                if (cmbDataItem->itemData(i, Qt::UserRole).toString() == qName)
                    return i;
            }
            return -1;
        };

        // 同步 ComboList 当前选中项
        if (cmbDataItem)
        {
            cmbDataItem->blockSignals(true);
            if (yColName.empty())
            {
                cmbDataItem->setCurrentIndex(-1);
            }
            else
            {
                int idx = findCmbIdx(yColName);
                if (idx >= 0)
                    cmbDataItem->setCurrentIndex(idx);
            }
            cmbDataItem->blockSignals(false);
        }

        if (yColName.empty())
        {
            return;
        }

        // ---- 同步表达式编辑栏 ----
        auto* lineEdit = m_exprLineEdits.value(pageIndex, nullptr);
        if (lineEdit && !yColName.empty())
        {
            auto& exprMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr;
            viewer::PlotExpression* pe = exprMgr.get(yColName);
            lineEdit->blockSignals(true);
            if (pe)
                lineEdit->setText(QString::fromStdString(pe->expressionText));
            else
                lineEdit->setText(QString::fromStdString(yColName));
            lineEdit->blockSignals(false);
        }

        // ---- 更新工具栏 combo 星号显示 ----
        if (cmbDataItem && !yColName.empty())
        {
            auto& exprMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr;
            viewer::PlotExpression* pe = exprMgr.get(yColName);
            if (pe)
            {
                QString displayName = QString::fromStdString(yColName);
                if (pe->isEdited)
                    displayName += "*";
                int ci = findCmbIdx(yColName);
                if (ci >= 0 && cmbDataItem->itemText(ci) != displayName)
                {
                    cmbDataItem->blockSignals(true);
                    cmbDataItem->setItemText(ci, displayName);
                    cmbDataItem->blockSignals(false);
                }
            }
        }

        // 查找 graph
        viewer::QCPColumnGraph* graph = nullptr;
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* p = plot->plottable(i);
            if (p && p->name().toStdString() == yColName)
            {
                graph = dynamic_cast<viewer::QCPColumnGraph*>(p);
                break;
            }
        }
        if (!graph)
        {
            return;
        }

        // 更新控件值（阻断信号避免递归触发）
        auto guard = [](auto* w, auto fn) {
            if (!w) return;
            w->blockSignals(true);
            fn();
            w->blockSignals(false);
        };

        QPen pen = graph->pen();
        // 线型映射：Qt::SolidLine=0, Qt::DotLine=1, Qt::DashLine=2, Qt::DashDotLine=3
        static const std::map<Qt::PenStyle, int> styleMap = {
            {Qt::SolidLine, 0}, {Qt::DotLine, 1}, {Qt::DashLine, 2}, {Qt::DashDotLine, 3}
        };
        guard(cmbLS, [&]{ cmbLS->setCurrentIndex(styleMap.count(pen.style()) ? styleMap.at(pen.style()) : 0); });
        guard(spnLW, [&]{ spnLW->setValue(pen.width()); });

        guard(cmbLC, [&]{
            int ci = -1;
            for (int i = 0; i < cmbLC->count(); ++i) {
                QColor c = cmbLC->itemData(i, Qt::UserRole).value<QColor>();
                if (c == pen.color()) { ci = i; break; }
            }
            if (ci >= 0) cmbLC->setCurrentIndex(ci);
        });

        QCPScatterStyle ss = graph->scatterStyle();
        // 散点形状映射
        static const std::map<QCPScatterStyle::ScatterShape, int> shapeMap = {
            {QCPScatterStyle::ssNone, 0}, {QCPScatterStyle::ssCircle, 1}, {QCPScatterStyle::ssDisc, 2},
            {QCPScatterStyle::ssSquare, 3}, {QCPScatterStyle::ssDiamond, 4}, {QCPScatterStyle::ssStar, 5}
        };
        guard(cmbSC, [&]{ cmbSC->setCurrentIndex(shapeMap.count(ss.shape()) ? shapeMap.at(ss.shape()) : 0); });
        guard(spnSS, [&]{ spnSS->setValue(static_cast<int>(ss.size())); });

        guard(cmbSCo, [&]{
            int ci = -1;
            for (int i = 0; i < cmbSCo->count(); ++i) {
                QColor c = cmbSCo->itemData(i, Qt::UserRole).value<QColor>();
                if (c == ss.pen().color()) { ci = i; break; }
            }
            if (ci >= 0) cmbSCo->setCurrentIndex(ci);
        });
    };

    // 清空全部图窗 → 清理 QTabWidget
    pm.onCleared = [this]()
    {
        m_exprLineEdits.clear();
        m_toolbarCombos.clear();
        while (m_plotTabs->count() > 0)
        {
            QWidget* w = m_plotTabs->widget(0);
            m_plotTabs->removeTab(0);
            delete w;
        }
    };
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

    // 向 PlotManager 注册（自动处理去重）
    pm.addDataToActivePage(yColName);
}

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

// ============================================================
// CursorManager → Qt 控件绑定
// ============================================================

void UI::bindCursorManagerCallbacks()
{
    auto& cm = m_viewer.GetCursorManager();
    auto& pm = m_viewer.GetPlotManager();

    // ---- 扩展 onPageAdded：在 plot 创建后附加 tracer ----
    auto originalOnPageAdded = pm.onPageAdded;

    pm.onPageAdded = [this, originalOnPageAdded](int index)
    {
        if (originalOnPageAdded)
            originalOnPageAdded(index);

        if (index < 0 || index >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(index);
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
    };

    // ---- 扩展 onPageAboutToRemove：清理该页面的 tracer 和相关游标 ----
    auto originalOnPageAboutToRemove = pm.onPageAboutToRemove;

    pm.onPageAboutToRemove = [this, originalOnPageAboutToRemove](int index)
    {
        // 获取即将被移除的 plot，清理 m_preSelTracers
        if (index >= 0 && index < m_plotTabs->count())
        {
            auto* container = m_plotTabs->widget(index);
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

        // 清除可能存在的预选
        cm.clearPreSelection();

        // ---- 清理高亮元素映射 ----
        m_highlightRects.remove(index);
        m_highlightLabels.remove(index);

        // 执行原有逻辑（删除 widget）
        if (originalOnPageAboutToRemove)
            originalOnPageAboutToRemove(index);
    };

    // ---- 扩展 onCleared：清理所有 cursor 状态 ----
    auto originalOnCleared = pm.onCleared;
    pm.onCleared = [this, originalOnCleared]()
    {
        auto& cm = m_viewer.GetCursorManager();
        cm.clearAll();
        m_plotToPageIndex.clear();
        m_preSelTracers.clear();
        m_cursorTracers.clear();
        m_cursorLabels.clear();
        m_highlightRects.clear();
        m_highlightLabels.clear();

        if (originalOnCleared)
            originalOnCleared();
    };

    // ---- 预选设置 → 显示 tracer + 更新状态栏 ----
    cm.onPreSelectionSet = [this](int pageIndex, const std::string& dataItemName, size_t dataIndex)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
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
                if (plot)
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
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
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

        // 创建 QCPItemText 数据标签（锚定在 plot 坐标系）
        auto* label = new QCPItemText(plot);
        label->position->setType(QCPItemPosition::ptPlotCoords);
        label->position->setCoords(x, y);
        label->setText(QString("X: %1\nY: %2")
                           .arg(x, 0, 'g', 8)
                           .arg(y, 0, 'g', 8));
        label->setFont(QFont("Consolas, Courier New, monospace", 9));
        label->setSelectable(false);
        label->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
        label->setPadding(QMargins(6, 3, 6, 3));
        label->setLayer("overlay");
        m_cursorLabels[cursorIdx] = label;
        refreshCursorLabelStyle(cursorIdx, true);  // 新游标 → 激活态

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

        plot->replot();
    };

    // ---- 游标移除 → 删除 QCPItemText 标签 + tracer ----
    cm.onCursorRemoved = [this](int cursorIdx)
    {
        // 删除 QCPItemText 标签
        auto lit = m_cursorLabels.find(cursorIdx);
        if (lit != m_cursorLabels.end())
        {
            QCPItemText* label = lit.value();
            QCustomPlot* lplot = label ? label->parentPlot() : nullptr;
            if (lplot)
                lplot->removeItem(label);
            m_cursorLabels.erase(lit);
            if (lplot)
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
            if (tplot)
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
    };

    // ---- 激活游标变更 → 更新标签样式 + tracer 样式 + 内容 ----
    cm.onActiveCursorChanged = [this](int cursorIdx)
    {
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
                if (cursor.pageIndex >= 0 && cursor.pageIndex < m_plotTabs->count())
                {
                    auto* container = m_plotTabs->widget(cursor.pageIndex);
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
                                lbIt.value()->position->setCoords(x, y);
                                lbIt.value()->setText(QString("X: %1\nY: %2").arg(x, 0, 'g', 8).arg(y, 0, 'g', 8));
                            }
                            plot->replot();
                        }
                    }
                }
            }
        }
    };
}

// ============================================================
// findNearestDataPoint: 在 plot 的所有 graph 中查找最近的数据点
// ============================================================

QPair<viewer::QCPColumnGraph*, size_t> UI::findNearestDataPoint(
    QCustomPlot* plot, const QPoint& mousePos, double pixelThreshold) const
{
    viewer::QCPColumnGraph* bestGraph = nullptr;
    size_t bestIdx = 0;
    double bestDistSq = pixelThreshold * pixelThreshold;

    for (int i = 0; i < plot->plottableCount(); ++i)
    {
        auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
        if (!graph)
            continue;

        int n = graph->dataCount();
        if (n == 0)
            continue;

        // 将鼠标像素坐标转为 key 坐标，二分查找最近的 index
        double mouseKey = plot->xAxis->pixelToCoord(mousePos.x());
        double mouseValue = plot->yAxis->pixelToCoord(mousePos.y());

        // 二分查找第一个 >= mouseKey 的索引
        int lo = 0, hi = n - 1;
        while (lo < hi)
        {
            int mid = (lo + hi) / 2;
            if (graph->dataMainKey(mid) < mouseKey)
                lo = mid + 1;
            else
                hi = mid;
        }
        int closestIdx = lo;

        // 检查左右邻居（±50范围内，限制搜索）
        int searchRange = 50;
        int start = std::max(0, closestIdx - searchRange);
        int end   = std::min(n - 1, closestIdx + searchRange);

        for (int j = start; j <= end; ++j)
        {
            QPointF pixelPos = graph->dataPixelPosition(j);
            double dx = pixelPos.x() - mousePos.x();
            double dy = pixelPos.y() - mousePos.y();
            double distSq = dx * dx + dy * dy;

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestGraph = graph;
                bestIdx = static_cast<size_t>(j);
            }
        }
    }

    return { bestGraph, bestIdx };
}

// ============================================================
// isDataTooDense: 检查当前图窗数据密度
// ============================================================

bool UI::isDataTooDense(QCustomPlot* plot) const
{
    if (!plot)
        return true;

    if (plot->plottableCount() == 0)
        return true;

    // 当前 X 轴可见范围（数据坐标）
    const QCPRange& keyRange = plot->xAxis->range();

    int totalVisiblePoints = 0;
    for (int i = 0; i < plot->plottableCount(); ++i)
    {
        auto* graph = dynamic_cast<viewer::QCPColumnGraph*>(plot->plottable(i));
        if (!graph || graph->dataCount() == 0)
            continue;

        int begin = graph->findBegin(keyRange.lower, true);
        int end   = graph->findEnd(keyRange.upper, true);
        if (end > begin)
            totalVisiblePoints += (end - begin);
    }

    if (totalVisiblePoints <= 0)
        return false;  // 没有可见点时放行

    int plotWidth = plot->axisRect() ? plot->axisRect()->width() : plot->width();
    if (plotWidth <= 0)
        return false;

    double density = static_cast<double>(totalVisiblePoints) / static_cast<double>(plotWidth);
    return density > 2.0;
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
    if (label->parentPlot())
        label->parentPlot()->replot();
}

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
            textItem->setFont(QFont("sans-serif", 9));
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

// ============================================================
// exportPlotImage: 导出当前图窗为图片
// ============================================================

void UI::exportPlotImage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
        return;

    auto* container = m_plotTabs->widget(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
        return;

    QString filter = "PNG (*.png);;JPEG (*.jpg);;PDF (*.pdf)";
    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出图片"), QString(),
        filter, &selectedFilter);

    if (fileName.isEmpty())
        return;

    if (selectedFilter.contains("PNG"))
        plot->savePng(fileName, 0, 0, 2.0, 100);
    else if (selectedFilter.contains("JPEG"))
        plot->saveJpg(fileName, 0, 0, 2.0, 90);
    else if (selectedFilter.contains("PDF"))
        plot->savePdf(fileName);
}

// ============================================================
// Alias: 自动重命名
// ============================================================

void UI::loadAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_aliasMap.clear();
    if (!doc.isObject())
        return;

    QJsonArray arr = doc.object().value("aliases").toArray();
    for (const auto& item : arr)
    {
        QJsonObject obj = item.toObject();
        std::string from = obj.value("from").toString().toStdString();
        std::string to   = obj.value("to").toString().toStdString();
        if (!from.empty() && !to.empty())
            m_aliasMap[from] = to;
    }

    m_viewer.GetDataManager().SetAliasMap(m_aliasMap);
}

void UI::saveAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";

    QJsonArray arr;
    for (const auto& [from, to] : m_aliasMap)
    {
        QJsonObject obj;
        obj["from"] = QString::fromStdString(from);
        obj["to"]   = QString::fromStdString(to);
        arr.append(obj);
    }

    QJsonObject root;
    root["aliases"] = arr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    m_viewer.GetDataManager().SetAliasMap(m_aliasMap);
}

void UI::showAliasDialog()
{
    AliasDialog dlg(this);
    dlg.setAliases(m_aliasMap);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_aliasMap = dlg.getAliases();
    saveAliasFile();
}
