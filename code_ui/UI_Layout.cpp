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

extern bool isSystemInDark();

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
    m_bookmarkTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkDock = new ads::CDockWidget("Bookmarks");
    m_bookmarkDock->setWidget(m_bookmarkTree);
    m_bookmarkDock->setFeatures(ads::CDockWidget::DockWidgetDeleteOnClose);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_bookmarkDock, m_plotDock->dockAreaWidget());

    connect(m_bookmarkTree, &QTreeWidget::itemDoubleClicked, this, &UI::onBookmarkDoubleClicked);
    connect(m_bookmarkTree, &QTreeWidget::customContextMenuRequested,
            this, &UI::onBookmarkTreeContextMenu);

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

void UI::createMenu()
{
    /** Generating MainWindow part */

    auto* fileMenu = menuBar()->addMenu("文件");
    auto* openCSV = fileMenu->addAction("载入多个CSV文件");
    auto* openFolder = fileMenu->addAction("载入多个文件夹下的全部CSV文件");
    auto* openBinary = fileMenu->addAction("载入JSON+二进制文件");
    fileMenu->addSeparator();
    auto* clearAll = fileMenu->addAction("清空全部数据");

    auto* settingsMenu = menuBar()->addMenu("设置");
    auto* aliasAction = settingsMenu->addAction("自动重命名数据");
    connect(aliasAction, &QAction::triggered, this, &UI::showAliasDialog);

    settingsMenu->addSeparator();

    auto* downsampleAction = settingsMenu->addAction(QString::fromUtf8("自适应降采样"));
    downsampleAction->setCheckable(true);
    downsampleAction->setChecked(m_adaptiveDownsampling);
    connect(downsampleAction, &QAction::toggled, this, [this](bool checked)
    {
        m_adaptiveDownsampling = checked;
        viewer::QCPColumnGraph::s_adaptiveSamplingEnabled = checked;
    });

    auto* openglAction = settingsMenu->addAction("OpenGL 绘制");
    openglAction->setCheckable(true);
    openglAction->setChecked(m_openglEnabled);
    connect(openglAction, &QAction::toggled, this, [this](bool checked)
    {
        m_openglEnabled = checked;
    });

    auto* antiAliasingAction = settingsMenu->addAction(QString::fromUtf8("曲线抗锯齿"));
    antiAliasingAction->setCheckable(true);
    antiAliasingAction->setChecked(m_antiAliasingEnabled);
    connect(antiAliasingAction, &QAction::toggled, this, [this](bool checked)
    {
        m_antiAliasingEnabled = checked;
        viewer::QCPColumnGraph::s_antiAliasingEnabled = checked;
    });

    auto* aboutMenu = menuBar()->addMenu("关于");
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
