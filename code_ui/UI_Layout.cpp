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
#include <qwindow.h>
#include <utility>
#include <cmath>

#include "icons_base64.h"
#include "DockAreaWidget.h"
#include "DockAreaTitleBar.h"
#include "FloatingDockContainer.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
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
#include <windows.h>
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
        m_pendingSkippedFiles = files;
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
            QMessageBox::warning(this, QString::fromUtf8("X轴数据告警"), monotonicWarning);
    });

    // Handle cross-file column validation errors
    connect(&m_viewer, &viewer::Viewer::LoadError, this, [this](const QString& message)
    {
        m_progressBar->setValue(0);
        m_progressBar->hide();
        m_dataTree->clear();
        m_xAxisLabel->setText("X: (none)");
        QMessageBox::critical(this, "CSV载入错误", message);
    });
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

		// 记录需要连接信号的 action
		switch (entry.id)
		{
		case IconIdx::LOADCSV:  action_loadcsv  = action; break;
        case IconIdx::LOADFOLDER: action_loadfolder = action; break;
		case IconIdx::CLEAR:    action_clearall = action; break;
		case IconIdx::VARRENAME: action_varrename = action; break;
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
	const auto dwMap = m_plotDockManager->dockWidgetsMap();
	for (auto it = dwMap.begin(); it != dwMap.end(); ++it)
	{
		if (it.value() != dock && it.value()->dockAreaWidget())
		{
			targetArea = it.value()->dockAreaWidget();
			break;
		}
	}

	if (targetArea)
		m_plotDockManager->addDockWidgetTabToArea(dock, targetArea);
	else
		m_plotDockManager->addDockWidget(ads::CenterDockWidgetArea, dock);

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
            if (m_isShuttingDown)
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
            if (m_isShuttingDown)
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
                    if (m_isShuttingDown)
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
