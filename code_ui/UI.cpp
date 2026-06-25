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

#include "icons_base64.h"
#include "code_viewer/datamgr/qcp_chunked_graph.h"

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

    QSettings settings;

    if (settings.contains("geometry"))
    {
        restoreGeometry(settings.value("geometry").toByteArray());
    }

    if (settings.contains("dockingState"))
    {
        m_dockManager->restoreState(settings.value("dockingState").toByteArray());
    }
}

UI::~UI()
{
    saveState();
}

void UI::init()
{
    setCentralWidget(m_dockManager);

    

    createMenu();
    createMain();
    createToolbar();
    createStatusbar();
}

void UI::createMenu()
{
    /** Generating MainWindow part */

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openCSV = fileMenu->addAction("Open CSV file");
    auto* openFolder = fileMenu->addAction("Open CSV files in folders");
    fileMenu->addSeparator();
    auto* clearAll = fileMenu->addAction("Clear loaded data");

    /** Connecting */

    //connect(openCSV, &QAction::triggered, this, &UI::)
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

    ///< Bind PlotManager callbacks
    bindPlotManagerCallbacks();
}

void UI::createStatusbar()
{
    // X-axis label (left side)
    m_xAxisLabel = new QLabel("X: (none)");
    statusBar()->addWidget(m_xAxisLabel);

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
    QSettings settings; // Automatically opens: <exe_dir>/MyCompany/MyApp.ini

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

    // 35 色预设（参考 MATLAB, 最后为黑和两种灰）
    static const QColor colorPresets[] = {
        // Row 1: MATLAB 7 色
        QColor(0, 114, 189),     // 0:  MATLAB Blue
        QColor(217, 83, 25),     // 1:  MATLAB Orange
        QColor(237, 177, 32),    // 2:  MATLAB Yellow
        QColor(126, 47, 142),    // 3:  MATLAB Purple
        QColor(119, 172, 48),    // 4:  MATLAB Green
        QColor(77, 190, 238),    // 5:  MATLAB Cyan
        QColor(162, 20, 47),     // 6:  MATLAB Red
        // Row 2: Deep saturated
        QColor(0, 147, 147),     // 7:  Teal
        QColor(255, 61, 127),    // 8:  Pink
        QColor(100, 149, 237),   // 9:  Cornflower
        QColor(205, 92, 92),     // 10: Indian Red
        QColor(85, 107, 47),     // 11: Olive Drab
        QColor(186, 85, 211),    // 12: Medium Orchid
        // Row 3: Bright accents
        QColor(0, 191, 255),     // 13: Deep Sky Blue
        QColor(255, 215, 0),     // 14: Gold
        QColor(50, 205, 50),     // 15: Lime Green
        QColor(255, 99, 71),     // 16: Tomato
        QColor(64, 224, 208),    // 17: Turquoise
        QColor(255, 140, 0),     // 18: Dark Orange
        // Row 4: Rich tones
        QColor(138, 43, 226),    // 19: Blue Violet
        QColor(0, 206, 209),     // 20: Dark Turquoise
        QColor(255, 20, 147),    // 21: Deep Pink
        QColor(154, 205, 50),    // 22: Yellow Green
        QColor(70, 130, 180),    // 23: Steel Blue
        QColor(240, 128, 128),   // 24: Light Coral
        // Row 5: More
        QColor(147, 112, 219),   // 25: Medium Purple
        QColor(60, 179, 113),    // 26: Medium Sea Green
        QColor(255, 160, 122),   // 27: Light Salmon
        QColor(0, 191, 143),     // 28: Mint
        QColor(255, 69, 0),      // 29: Orange Red
        QColor(65, 105, 225),    // 30: Royal Blue
        QColor(218, 165, 32),    // 31: Goldenrod
        // End markers
        QColor(Qt::black),       // 32: Black
        QColor(128, 128, 128),   // 33: Gray
        QColor(192, 192, 192)    // 34: Silver
    };
    static const int kColorCount = sizeof(colorPresets) / sizeof(colorPresets[0]);

    // 页面添加 → 创建新 QCustomPlot 页（含工具栏）
    pm.onPageAdded = [this](int index)
    {
        // ---- QCustomPlot ----
        auto* plot = new QCustomPlot();
        plot->setOpenGl(true);
        plot->setInteraction(QCP::iRangeDrag, true);
        plot->setInteraction(QCP::iRangeZoom, true);
        plot->setInteraction(QCP::iSelectPlottables, true);
        plot->xAxis->setLabel("X");
        plot->yAxis->setLabel("Y");

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

                menu.addAction("更改样式");
                menu.addSeparator();
                menu.addAction("编辑数据项");
                menu.addAction("添加表达式");
                menu.addAction("高亮规则");
                menu.addSeparator();
                menu.addAction("导出图片");
                menu.addAction("加入收藏夹");
                menu.addSeparator();
                menu.addAction("关闭图表");
                menu.exec(plot->mapToGlobal(pos));
            });

        // ---- 工具栏 ----
        auto* toolbar = new QWidget();
        toolbar->setFixedHeight(32);
        auto* hbox = new QHBoxLayout(toolbar);
        hbox->setContentsMargins(2, 2, 2, 2);
        hbox->setSpacing(4);

        // 1. 文本框：选中数据项名
        auto* edtItem = new QLineEdit();
        edtItem->setReadOnly(true);
        edtItem->setPlaceholderText("No selection");
        edtItem->setMaximumWidth(120);
        hbox->addWidget(edtItem);

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
        for (int i = 0; i < kColorCount; ++i)
        {
            QPixmap swatch(20, 14);
            swatch.fill(colorPresets[i]);
            cmbLineColor->addItem(QIcon(swatch), QString(), QVariant::fromValue(colorPresets[i]));
        }
        cmbLineColor->setCurrentIndex(8); // "Red"
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
        for (int i = 0; i < kColorCount; ++i)
        {
            QPixmap swatch(20, 14);
            swatch.fill(colorPresets[i]);
            cmbScatterColor->addItem(QIcon(swatch), QString(), QVariant::fromValue(colorPresets[i]));
        }
        cmbScatterColor->setCurrentIndex(8); // "Red"
        hbox->addWidget(cmbScatterColor);

        hbox->addStretch();

        // ---- 容器 ----
        auto* container = new QWidget();
        auto* vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        vbox->addWidget(toolbar);
        vbox->addWidget(plot, 1); // plot 占剩余空间

        // ---- 数据项选中 → PlotManager ----
        connect(plot, &QCustomPlot::selectionChangedByUser, this,
            [this, plot, index]()
            {
                auto& pm = m_viewer.GetPlotManager();
                std::string selected;
                for (int i = 0; i < plot->plottableCount(); ++i)
                {
                    auto* p = plot->plottable(i);
                    if (p && p->selected())
                    {
                        selected = p->name().toStdString();
                        break;
                    }
                }
                pm.setSelectedDataItem(index, selected);
            });

        // ---- 工具栏控件→graph 回写辅助 ----
        auto applyToGraph = [this, plot, index]()
        {
            auto& pm = m_viewer.GetPlotManager();
            const auto& selName = pm.selectedDataItem(index);
            if (selName.empty())
                return;

            viewer::QCPChunkedGraph* graph = nullptr;
            for (int i = 0; i < plot->plottableCount(); ++i)
            {
                auto* p = plot->plottable(i);
                if (p && p->name().toStdString() == selName)
                {
                    graph = dynamic_cast<viewer::QCPChunkedGraph*>(p);
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
            this, applyToGraph);
        connect(spnLineWidth, QOverload<int>::of(&QSpinBox::valueChanged),
            this, applyToGraph);
        connect(cmbLineColor, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyToGraph);
        connect(cmbScatter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyToGraph);
        connect(spnScatterSize, QOverload<int>::of(&QSpinBox::valueChanged),
            this, applyToGraph);
        connect(cmbScatterColor, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, applyToGraph);

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
    };

    // 数据项添加 → 创建 QCPChunkedGraph 并绑定到对应 QCustomPlot
    pm.onDataItemAdded = [this](int pageIndex, const std::string& yColName)
    {
        if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
            return;

        auto* container = m_plotTabs->widget(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        auto& dm = m_viewer.GetDataManager();
        size_t xIdx = dm.GetXAxisColumn();
        const viewer::AbstractColumn* xCol = dm.GetColumn(xIdx);
        const viewer::AbstractColumn* yCol = dm.GetColumn(yColName);
        if (!xCol || !yCol)
            return;

        // 创建 QCPChunkedGraph
        auto* graph = new viewer::QCPChunkedGraph(plot->xAxis, plot->yAxis);
        graph->setDataColumns(xCol, yCol);

        // 设置默认外观（使用 colorPresets 确保颜色在工具栏中可匹配）
        QPen pen(graph->pen());
        size_t plotCount = m_viewer.GetPlotManager().pageInfo(pageIndex).dataItems.size();
        if (plotCount > 0)
        {
            pen.setColor(colorPresets[(plotCount - 1) % kColorCount]);
        }
        graph->setPen(pen);

        // 名称设置为 Y 列名
        graph->setName(QString::fromStdString(yColName));

        // 缩放轴以包含数据（第一个图完全匹配，后续图仅扩大）
        graph->rescaleAxes(plotCount > 1);
        plot->replot();
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

        // 查找并移除对应名称的 QCPChunkedGraph
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
        if (!hb || hb->count() < 7)
            return;

        auto* edtItem   = qobject_cast<QLineEdit*>(hb->itemAt(0)->widget());
        auto* cmbLS     = qobject_cast<QComboBox*>(hb->itemAt(1)->widget());
        auto* spnLW     = qobject_cast<QSpinBox*>(hb->itemAt(2)->widget());
        auto* cmbLC     = qobject_cast<QComboBox*>(hb->itemAt(3)->widget());
        auto* cmbSC     = qobject_cast<QComboBox*>(hb->itemAt(4)->widget());
        auto* spnSS     = qobject_cast<QSpinBox*>(hb->itemAt(5)->widget());
        auto* cmbSCo    = qobject_cast<QComboBox*>(hb->itemAt(6)->widget());

        if (yColName.empty())
        {
            // 取消选中
            if (edtItem) edtItem->clear();
            return;
        }

        // 查找 graph
        viewer::QCPChunkedGraph* graph = nullptr;
        for (int i = 0; i < plot->plottableCount(); ++i)
        {
            auto* p = plot->plottable(i);
            if (p && p->name().toStdString() == yColName)
            {
                graph = dynamic_cast<viewer::QCPChunkedGraph*>(p);
                break;
            }
        }
        if (!graph)
        {
            if (edtItem) edtItem->clear();
            return;
        }

        // 更新控件值（阻断信号避免递归触发）
        auto guard = [](auto* w, auto fn) {
            if (!w) return;
            w->blockSignals(true);
            fn();
            w->blockSignals(false);
        };

        guard(edtItem, [&]{ edtItem->setText(QString::fromStdString(yColName)); });

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

    // 向 PlotManager 注册（自动处理去重和自动创建图窗）
    bool added = pm.addDataToActivePage(yColName);

    // 如果去重返回 false(数据项已存在) 或手动创建了新页面，需要触发图表创建
    // addDataToActivePage 内部会触发 onDataItemAdded 回调自动创建 QCPChunkedGraph
    // 仅当添加成功时才需要处理；如果已存在，UI 层无需额外操作
    (void)added;
}

// ============================================================
// 事件过滤器：Ctrl/Shift + 滚轮 单轴缩放
// ============================================================

bool UI::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu)
    {
        // 框选模式下右键取消框选
        QCustomPlot* plot = qobject_cast<QCustomPlot*>(obj);
        if (plot)
        {
            int pageIndex = m_plotTabs->indexOf(plot);
            if (pageIndex >= 0 && m_viewer.GetPlotManager().isRectZoomActive(pageIndex))
            {
                m_viewer.GetPlotManager().setRectZoomActive(pageIndex, false);
                return true;  // 吞掉事件，不弹出菜单
            }
        }
    }

    if (event->type() == QEvent::Wheel)
    {
        QCustomPlot* plot = qobject_cast<QCustomPlot*>(obj);
        if (plot)
        {
            QWheelEvent* we = static_cast<QWheelEvent*>(event);
            Qt::KeyboardModifiers mods = we->modifiers();

            if (mods & Qt::ControlModifier)
            {
                // Ctrl+滚轮：仅缩放 X 轴
                plot->axisRect()->setRangeZoom(Qt::Horizontal);
                plot->removeEventFilter(this);
                QCoreApplication::sendEvent(plot, event);
                plot->installEventFilter(this);
                plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
                return true;
            }
            else if (mods & Qt::ShiftModifier)
            {
                // Shift+滚轮：仅缩放 Y 轴
                plot->axisRect()->setRangeZoom(Qt::Vertical);
                plot->removeEventFilter(this);
                QCoreApplication::sendEvent(plot, event);
                plot->installEventFilter(this);
                plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}


