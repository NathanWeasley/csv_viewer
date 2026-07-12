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
#include <qcheckbox.h>
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
        for (int pi = 0; pi < plotPageCount(); ++pi)
        {
            auto* container = getPlotContainer(pi);
            if (!container) continue;
            auto* vbox = container->findChild<QVBoxLayout*>();
            if (!vbox || vbox->count() < 1) continue;
            auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
            if (!toolbar) continue;
            auto* hb = toolbar->findChild<QHBoxLayout*>();
            if (!hb || hb->count() < 11) continue;

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
        // QCustomPlot 的 OpenGL FBO 默认 16x MSAA，多个图窗时显存/内存占用会急剧增大。
        // 这里改为 0，优先控制多窗口场景下的内存占用。
        configurePlotDrawingMode(plot, m_openglEnabled);
        // overlay 默认会独占一个缓冲层，在 OpenGL FBO 模式下会额外复制一份大缓冲。
        // 这里改为 logical，可以减少多窗口场景下的内存占用和读回次数。
        plot->setNoAntialiasingOnDrag(true);
        plot->setPlottingHint(QCP::phFastPolylines, true);
        plot->setInteraction(QCP::iRangeDrag, true);
        plot->setInteraction(QCP::iRangeZoom, true);
        plot->xAxis->setLabel("X");
        plot->yAxis->setLabel("Y");
        logPlotTrace(QString("plot create page=%1 openGl=%2 overlayMode=%3 widgetDpr=%4")
                     .arg(index)
                     .arg(plot->openGl())
                     .arg(plot->layer("overlay") && plot->layer("overlay")->mode() == QCPLayer::lmLogical ? "logical" : "buffered")
                     .arg(QString::number(plot->devicePixelRatioF(), 'f', 2)));

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

        // 副网格
        {
            QPen subGridPen = plot->xAxis->basePen();
            subGridPen.setStyle(Qt::DotLine);
            QColor subGridColor = subGridPen.color();
            subGridColor.setAlpha(60);
            subGridPen.setColor(subGridColor);
            plot->xAxis->grid()->setSubGridPen(subGridPen);
            plot->yAxis->grid()->setSubGridPen(subGridPen);
        }
        plot->xAxis->grid()->setSubGridVisible(true);
        plot->yAxis->grid()->setSubGridVisible(true);

        // 事件过滤器
        plot->installEventFilter(this);

        // 图例交互
        plot->setInteraction(QCP::iSelectLegend, true);

        // X 轴联动：任一图窗的横轴范围变化后，同步到所在联动组的其他图窗。
        connect(plot->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
            this, [this, plot](const QCPRange& newRange)
            {
                const int pageIndex = m_plotToPageIndex.value(plot, -1);
                if (pageIndex >= 0)
                    syncLinkedXAxisRange(pageIndex, newRange);
            });

        // 右键上下文菜单
        plot->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(plot, &QCustomPlot::customContextMenuRequested, this,
            [this, plot](const QPoint& pos)
            {
                auto& pm = m_viewer.GetPlotManager();
                int pageIndex = m_plotToPageIndex.value(plot, -1);
                if (pageIndex < 0 || pageIndex >= pm.pageCount())
                    return;
                bool isFFT = pm.isFFTPage(pageIndex);

                QMenu menu;

                QAction* newPageAction = menu.addAction("新建图窗");
                connect(newPageAction, &QAction::triggered, this, [this]()
                {
                    m_viewer.GetPlotManager().addPage();
                });

                QAction* dupPageAction = menu.addAction("复制图窗");
                dupPageAction->setEnabled(!isFFT);
                connect(dupPageAction, &QAction::triggered, this, [this, plot, pageIndex]()
                {
                    auto& pm = m_viewer.GetPlotManager();

                    // 在 addPage() 之前复制数据项列表（避免 vector 扩容使引用失效）
                    const auto& srcDataItems = pm.pageInfo(pageIndex).dataItems;
                    std::vector<std::string> itemsToCopy(srcDataItems.begin(), srcDataItems.end());
                    bool legendOn = pm.pageInfo(pageIndex).legendVisible;

                    // 创建新图窗
                    int newIdx = pm.addPage();

                    // ---- 复制 X 轴配置 ----
                    pm.setXAxisColumn(newIdx, pm.pageInfo(pageIndex).xAxisColumn);

                    // ---- 先复制表达式数据（在 addDataItem 之前，避免 getOrCreate 创建的本地拷贝被替换）----
                    {
                        auto& srcExprMgr = pm.pageInfo(pageIndex).exprMgr;
                        auto& dstExprMgr = pm.pageInfo(newIdx).exprMgr;
                        dstExprMgr.insertAll(srcExprMgr.copyAll());
                    }

                    // ---- 抑制批量 addDataItem 导致的中间重绘 ----
                    auto* dstContainer = getPlotContainer(newIdx);
                    auto* dstPlot = dstContainer ? dstContainer->findChild<QCustomPlot*>() : nullptr;
                    if (dstPlot) dstPlot->setUpdatesEnabled(false);

                    // 复制所有数据项（onDataItemAdded 中的 getOrCreate 会发现已有表达式）
                    for (const auto& item : itemsToCopy)
                        pm.addDataItem(newIdx, item);

                    // 复制图例状态
                    if (legendOn)
                        pm.setLegendVisible(newIdx, true);

                    // ---- 复制高亮规则 ----
                    {
                        auto& srcHL = pm.pageInfo(pageIndex).highlightMgr;
                        auto& dstHL = pm.pageInfo(newIdx).highlightMgr;
                        dstHL.insertAllRules(srcHL.copyAllRules());
                    }

                    // ---- 复制样式：从源 plot 的 graph 复制 pen + scatter 到目标 plot ----
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

                            dstGraph->setVisible(srcGraph->visible());
                            dstGraph->setLineStyle(srcGraph->lineStyle());
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
                                if (cursors[ci].pageIndex == pageIndex)
                                    cm.addCursor(newIdx, cursors[ci].dataItemName, cursors[ci].dataIndex);
                            }
                        }

                        // ---- 复制对数轴状态 ----
                        dstPlot->xAxis->setScaleType(plot->xAxis->scaleType());
                        dstPlot->yAxis->setScaleType(plot->yAxis->scaleType());
                        {
                            auto* dstVbox = dstContainer->findChild<QVBoxLayout*>();
                            if (dstVbox && dstVbox->count() >= 1)
                            {
                                auto* dstToolbar = qobject_cast<QWidget*>(dstVbox->itemAt(0)->widget());
                                if (dstToolbar)
                                {
                                    auto* dstHb = dstToolbar->findChild<QHBoxLayout*>();
                                    if (dstHb && dstHb->count() >= 10)
                                    {
                                        bool srcLogX = (plot->xAxis->scaleType() == QCPAxis::stLogarithmic);
                                        bool srcLogY = (plot->yAxis->scaleType() == QCPAxis::stLogarithmic);
                                        auto* dstChkLogX = qobject_cast<QCheckBox*>(dstHb->itemAt(7)->widget());
                                        auto* dstChkLogY = qobject_cast<QCheckBox*>(dstHb->itemAt(8)->widget());
                                        if (dstChkLogX) { dstChkLogX->blockSignals(true); dstChkLogX->setChecked(srcLogX); dstChkLogX->blockSignals(false); }
                                        if (dstChkLogY) { dstChkLogY->blockSignals(true); dstChkLogY->setChecked(srcLogY); dstChkLogY->blockSignals(false); }
                                    }
                                }
                            }
                        }

                        dstPlot->setUpdatesEnabled(true);
                        dstPlot->rescaleAxes();
                        dstPlot->replot();
                    }
                });

                menu.addSeparator();

                QAction* rescaleAction = menu.addAction("还原缩放");
                connect(rescaleAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    if (auto& cb = m_viewer.GetPlotManager().onRescaleRequested)
                        cb(pageIndex);
                });

                QAction* rectZoomAction = menu.addAction("框选缩放");
                rectZoomAction->setCheckable(true);
                rectZoomAction->setChecked(pm.isRectZoomActive(pageIndex));
                connect(rectZoomAction, &QAction::toggled, this, [this, pageIndex](bool checked)
                {
                    m_viewer.GetPlotManager().setRectZoomActive(pageIndex, checked);
                });

                menu.addSeparator();

                QAction* legendAction = menu.addAction("显示图例");
                legendAction->setCheckable(true);
                legendAction->setChecked(pm.isLegendVisible(pageIndex));
                connect(legendAction, &QAction::toggled, this, [this, pageIndex](bool checked)
                {
                    m_viewer.GetPlotManager().setLegendVisible(pageIndex, checked);
                });

                QAction* highlightAction = menu.addAction("高亮规则");
                connect(highlightAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    showHighlightDialog(pageIndex);
                });

                menu.addSeparator();

                bool hasData = !pm.pageInfo(pageIndex).dataItems.empty();

                QAction* fftAction = menu.addAction("计算FFT");
                fftAction->setEnabled(hasData && !isFFT);
                connect(fftAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    onFFTRequested(pageIndex);
                });

                QAction* stftAction = menu.addAction("计算STFT");
                stftAction->setEnabled(hasData && !isFFT);
                connect(stftAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    onSTFTRequested(pageIndex);
                });

                menu.addSeparator();

                QAction* exportAction = menu.addAction("导出图片");
                connect(exportAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    exportPlotImage(pageIndex);
                });

                QAction* bookmarkAction = menu.addAction("加入收藏夹");
                bookmarkAction->setEnabled(hasData && !isFFT);
                connect(bookmarkAction, &QAction::triggered, this, [this, pageIndex]()
                {
                    addBookmark(pageIndex);
                });

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
        cmbLineStyle->addItems({"无", "散点", "实线", "点线", "虚线", "点划线"});
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
        cmbScatter->addItems({"无", "实心圆", "空心圆", "方形", "菱形", "星号"});
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

        // 8. 对数 X 轴复选框
        auto* chkLogX = new QCheckBox("log X");
        chkLogX->setToolTip("X 轴对数显示");
        chkLogX->setFixedHeight(22);
        hbox->addWidget(chkLogX);

        // 9. 对数 Y 轴复选框
        auto* chkLogY = new QCheckBox("log Y");
        chkLogY->setToolTip("Y 轴对数显示");
        chkLogY->setFixedHeight(22);
        hbox->addWidget(chkLogY);

        hbox->addStretch();

        // 10. 删除按钮（最右侧）
        auto* btnDelete = new QPushButton("删除曲线");
        btnDelete->setFixedSize(72, 24);
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

        // ---- 主题感知表达式样式辅助 ----
        auto exprStyleNormal = [&]() -> QString {
            if (isSystemInDark())
                return QStringLiteral(
                    "QLineEdit { border: 1px solid #555; border-radius: 3px; padding: 2px 6px; "
                    "background: #2a2a2a; color: #ddd; }"
                    "QLineEdit:focus { border-color: #FFD700; }");
            else
                return QStringLiteral(
                    "QLineEdit { border: 1px solid #aaa; border-radius: 3px; padding: 2px 6px; "
                    "background: #f5f5f5; color: #333; }"
                    "QLineEdit:focus { border-color: #2266cc; }");
        };
        auto exprStyleError = [&]() -> QString {
            if (isSystemInDark())
                return QStringLiteral(
                    "QLineEdit { border: 1px solid #cc3333; border-radius: 3px; padding: 2px 6px; "
                    "background: #2a2a2a; color: #ddd; }"
                    "QLineEdit:focus { border-color: #ff4444; }");
            else
                return QStringLiteral(
                    "QLineEdit { border: 1px solid #cc3333; border-radius: 3px; padding: 2px 6px; "
                    "background: #f5f5f5; color: #333; }"
                    "QLineEdit:focus { border-color: #ff4444; }");
        };

        auto* exprLineEdit = new QLineEdit();
        exprLineEdit->setPlaceholderText("expression...");
        exprLineEdit->setStyleSheet(exprStyleNormal());
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
            this, [this, plot, cmbDataItem](int /*idx*/)
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string yColName = nameData.toStdString();
                auto& pm = m_viewer.GetPlotManager();
                int pageIndex = m_plotToPageIndex.value(plot, -1);
                if (pageIndex < 0 || pageIndex >= pm.pageCount()) return;
                pm.setSelectedDataItem(pageIndex, yColName);
            });

        // ---- 删除按钮 → 从图窗移除数据曲线 ----
        connect(btnDelete, &QPushButton::clicked, this,
            [this, plot, cmbDataItem]()
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string selName = nameData.toStdString();
                auto& pm = m_viewer.GetPlotManager();
                int pageIndex = m_plotToPageIndex.value(plot, -1);
                if (pageIndex < 0 || pageIndex >= pm.pageCount()) return;
                pm.removeDataItem(pageIndex, selName);
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

            int lsIdx = cmbLS->currentIndex();
            // 线型索引：0=无(隐藏曲线)，1=散点，2=实线，3=点线，4=虚线，5=点划线
            if (lsIdx == 0)
            {
                graph->setVisible(false);
            }
            else
            {
                graph->setVisible(true);
                if (lsIdx == 1)
                {
                    graph->setLineStyle(viewer::QCPColumnGraph::lsNone);
                }
                else
                {
                    graph->setLineStyle(viewer::QCPColumnGraph::lsLine);

                    QPen pen = graph->pen();
                    static const Qt::PenStyle penStyles[] = {
                        Qt::NoPen, Qt::NoPen, Qt::SolidLine, Qt::DotLine, Qt::DashLine, Qt::DashDotLine
                    };
                    pen.setStyle(penStyles[lsIdx]);
                    pen.setWidth(spnLW->value());
                    QColor lineColor = cmbLC->currentData(Qt::UserRole).value<QColor>();
                    if (lineColor.isValid())
                        pen.setColor(lineColor);
                    graph->setPen(pen);
                }
            }

            // 散点样式由工具栏控件独立控制
            {
                QCPScatterStyle ss = graph->scatterStyle();
                int scIdx = cmbSC->currentIndex();
                int scatterSize = spnSS->value();
                QColor scatterColor = cmbSCo->currentData(Qt::UserRole).value<QColor>();
                if (lsIdx == 1 && scIdx <= 0)
                {
                    scIdx = 1;
                    scatterSize = 1;

                    QColor lineColor = cmbLC->currentData(Qt::UserRole).value<QColor>();
                    if (!lineColor.isValid())
                        lineColor = graph->pen().color();
                    scatterColor = lineColor;

                    cmbSC->blockSignals(true);
                    cmbSC->setCurrentIndex(scIdx);
                    cmbSC->blockSignals(false);

                    spnSS->blockSignals(true);
                    spnSS->setValue(scatterSize);
                    spnSS->blockSignals(false);

                    int colorIndex = -1;
                    for (int i = 0; i < cmbSCo->count(); ++i)
                    {
                        if (cmbSCo->itemData(i, Qt::UserRole).value<QColor>() == scatterColor)
                        {
                            colorIndex = i;
                            break;
                        }
                    }
                    if (colorIndex >= 0)
                    {
                        cmbSCo->blockSignals(true);
                        cmbSCo->setCurrentIndex(colorIndex);
                        cmbSCo->blockSignals(false);
                    }
                }
                static const QCPScatterStyle::ScatterShape shapes[] = {
                    QCPScatterStyle::ssNone, QCPScatterStyle::ssDisc, QCPScatterStyle::ssCircle,
                    QCPScatterStyle::ssSquare, QCPScatterStyle::ssDiamond, QCPScatterStyle::ssStar
                };
                ss.setShape(shapes[scIdx]);
                ss.setSize(scatterSize);
                if (ss.shape() == QCPScatterStyle::ssNone)
                {
                    QColor lineColor = cmbLC->currentData(Qt::UserRole).value<QColor>();
                    if (!lineColor.isValid())
                        lineColor = graph->pen().color();
                    scatterColor = lineColor;
                }
                if (scatterColor.isValid())
                {
                    ss.setPen(QPen(scatterColor));
                    if (ss.shape() == QCPScatterStyle::ssCircle)
                        ss.setBrush(Qt::NoBrush);
                    else
                        ss.setBrush(QBrush(scatterColor));
                }
                graph->setScatterStyle(ss);
            }

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

        // ---- 对数轴复选框 → 切换坐标轴类型 ----
        connect(chkLogX, &QCheckBox::toggled, plot, [plot](bool on){
            plot->xAxis->setScaleType(on ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
            plot->replot();
        });
        connect(chkLogY, &QCheckBox::toggled, plot, [plot](bool on){
            plot->yAxis->setScaleType(on ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
            plot->replot();
        });

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
        setPlotPageBaseChrome(index, true, true);

        // ---- 表达式编辑框 textChanged → 合法性检查 + 计算 ----
        connect(exprLineEdit, &QLineEdit::textChanged, this,
            [this, plot, cmbDataItem, exprLineEdit, findComboIndexByUserRole, exprStyleNormal, exprStyleError](const QString& text)
            {
                QString nameData = cmbDataItem->currentData(Qt::UserRole).toString();
                if (nameData.isEmpty()) return;
                std::string selName = nameData.toStdString();

                auto& dm = m_viewer.GetDataManager();
                auto& pm = m_viewer.GetPlotManager();
                int pageIndex = m_plotToPageIndex.value(plot, -1);
                if (pageIndex < 0 || pageIndex >= pm.pageCount()) return;
                auto& exprMgr = pm.pageInfo(pageIndex).exprMgr;

                // 更新表达式文本
                exprMgr.setExpressionText(selName, text.toStdString());

                // 用 exprtk 检查合法性
                std::string exprStr = text.toStdString();
                if (exprStr.empty())
                {
                    exprLineEdit->setStyleSheet(exprStyleNormal());
                    return;
                }

                if (!exprMgr.validate(exprStr, dm))
                {
                    // 不合法：红色边框提示
                    exprLineEdit->setStyleSheet(exprStyleError());
                    return;
                }

                // 合法：恢复正常边框 + 计算
                exprLineEdit->setStyleSheet(exprStyleNormal());

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
                            size_t xIdx = m_viewer.GetPlotManager().xAxisColumn(pageIndex);
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
        addPlotPageDock(index, container, title);

        // 聚焦新页面（dock widget 就绪后激活）
        m_pendingActivation = index;

        if (m_viewer.GetPlotManager().layoutMode() != viewer::PlotLayoutMode::Tabbed)
        {
            m_savedTabbedPlotLayoutState.clear();
            m_hasSavedTabbedPlotLayoutState = false;
            if (m_viewer.GetPlotManager().layoutMode() == viewer::PlotLayoutMode::Grid)
                arrangePlotsInGridLayout();
            else
                arrangePlotsInRowLayout();
            updatePlotPageChromeForLayout(m_viewer.GetPlotManager().layoutMode());
            updatePlotLayoutActions(m_viewer.GetPlotManager().layoutMode());
        }
    };

    // 页面即将移除
    pm.onPageAboutToRemove = [this](int index)
    {
        logShutdownTrace(QString("pm.onPageAboutToRemove enter index=%1 pageCount=%2")
                             .arg(index)
                             .arg(plotPageCount()));
        if (index >= 0 && index < plotPageCount())
        {
            // removePlotPageDock 会通过 dock->deleteLater() 销毁整个 widget 层级
            m_syncingPlotRemoval = true;
            removePlotPageDock(index);
            m_syncingPlotRemoval = false;
        }
        logShutdownTrace(QString("pm.onPageAboutToRemove leave index=%1").arg(index));
    };

    // 页面移除后
    pm.onPageRemoved = [this](int activeIdx, int /*remainingCount*/)
    {
        logShutdownTrace(QString("pm.onPageRemoved activeIdx=%1 lastRemoved=%2")
                             .arg(activeIdx)
                             .arg(m_lastRemovedPageIndex));
        reindexPlotPageStateAfterRemoval(m_lastRemovedPageIndex);
        reindexLinkedXAxisGroupsAfterRemoval(m_lastRemovedPageIndex);
        m_lastRemovedPageIndex = -1;

        if (activeIdx >= 0 && activeIdx < plotPageCount())
        {
            auto& pm = m_viewer.GetPlotManager();
            pm.setActivePage(activeIdx);
        }

        if (m_viewer.GetPlotManager().layoutMode() != viewer::PlotLayoutMode::Tabbed)
        {
            m_savedTabbedPlotLayoutState.clear();
            m_hasSavedTabbedPlotLayoutState = false;
            if (m_viewer.GetPlotManager().layoutMode() == viewer::PlotLayoutMode::Grid)
                arrangePlotsInGridLayout();
            else
                arrangePlotsInRowLayout();
            updatePlotPageChromeForLayout(m_viewer.GetPlotManager().layoutMode());
            updatePlotLayoutActions(m_viewer.GetPlotManager().layoutMode());
        }
    };

    // 激活页面变更
    pm.onActivePageChanged = [this](int index)
    {
        // 同步内层 dock 聚焦（UI → 数据层方向）
        auto* dock = m_pageDocks.value(index, nullptr);
        if (dock && m_plotDockManager)
            m_plotDockManager->setDockWidgetFocused(dock);

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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;
        auto* container = getPlotContainer(pageIndex);
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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
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

        // 保存当前 updatesEnabled 状态，避免覆盖外层已设的 false
        bool prevUpdatesEnabled = plot->updatesEnabled();
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
                plot->removePlottable(graph);
                plot->setUpdatesEnabled(prevUpdatesEnabled);
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
        // 默认散点颜色跟随当前线色，避免后续切换到散点时回退到黑色。
        {
            QCPScatterStyle ss = graph->scatterStyle();
            ss.setPen(QPen(pen.color()));
            ss.setBrush(QBrush(pen.color()));
            graph->setScatterStyle(ss);
        }
        graph->setName(QString::fromStdString(yColName));

        // 创建表达式本地拷贝，切换到独立数据源
        auto& exprMgr = m_viewer.GetPlotManager().pageInfo(pageIndex).exprMgr;
        viewer::PlotExpression& pe = exprMgr.getOrCreate(yColName, dm);
        // 未编辑时 computedData 为 nullptr，直接从 DataManager 读取原始列
        const viewer::Column* yDataSource = pe.computedData.get();
        if (!yDataSource)
            yDataSource = dm.GetColumn(yColName);
        graph->setDataColumns(xCol, yDataSource);
        graph->notifyDataChanged();

        // 首个数据项全量缩放，后续项仅扩大
        graph->rescaleAxes(plotCount > 1);

        // 恢复外层 updatesEnabled 状态，仅当外层未禁用时才 replot
        plot->setUpdatesEnabled(prevUpdatesEnabled);
        if (prevUpdatesEnabled)
            plot->replot();

        // 向工具栏 ComboList 添加数据项名称
        auto* vbox = container->findChild<QVBoxLayout*>();
        if (!vbox || vbox->count() < 1)
            return;
        auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget());
        if (!toolbar)
            return;
        auto* hb = toolbar->findChild<QHBoxLayout*>();
        if (!hb || hb->count() < 11)
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
        auto* btnDeleteAdd = qobject_cast<QPushButton*>(hb->itemAt(10)->widget());
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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
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
        if (!hb || hb->count() < 11)
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
                auto* btnDeleteRm = qobject_cast<QPushButton*>(hb->itemAt(10)->widget());
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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        plot->replot();
    };

    // 框选缩放状态变更
    pm.onRectZoomStateChanged = [this](int pageIndex, bool active)
    {
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
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
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot)
            return;

        plot->rescaleAxes();
        plot->replot();
    };

    // 选中数据项变更 → 刷新工具栏控件
    pm.onSelectedDataItemChanged = [this](int pageIndex, const std::string& yColName)
    {
        if (pageIndex < 0 || pageIndex >= plotPageCount())
            return;

        auto* container = getPlotContainer(pageIndex);
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
        if (!hb || hb->count() < 11)
            return;

        auto* cmbDataItem = qobject_cast<QComboBox*>(hb->itemAt(0)->widget());
        auto* cmbLS       = qobject_cast<QComboBox*>(hb->itemAt(1)->widget());
        auto* spnLW       = qobject_cast<QSpinBox*>(hb->itemAt(2)->widget());
        auto* cmbLC       = qobject_cast<QComboBox*>(hb->itemAt(3)->widget());
        auto* cmbSC       = qobject_cast<QComboBox*>(hb->itemAt(4)->widget());
        auto* spnSS       = qobject_cast<QSpinBox*>(hb->itemAt(5)->widget());
        auto* cmbSCo      = qobject_cast<QComboBox*>(hb->itemAt(6)->widget());
        auto* btnDelete   = qobject_cast<QPushButton*>(hb->itemAt(10)->widget());

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

        // 线型映射：0=无(隐藏曲线)，1=散点，2=实线，3=点线，4=虚线，5=点划线
        if (!graph->visible())
        {
            guard(cmbLS, [&]{ cmbLS->setCurrentIndex(0); });
        }
        else if (graph->lineStyle() == viewer::QCPColumnGraph::lsNone)
        {
            guard(cmbLS, [&]{ cmbLS->setCurrentIndex(1); });
        }
        else
        {
            QPen pen = graph->pen();
            static const std::map<Qt::PenStyle, int> styleMap = {
                {Qt::SolidLine, 2}, {Qt::DotLine, 3}, {Qt::DashLine, 4}, {Qt::DashDotLine, 5}
            };
            guard(cmbLS, [&]{ cmbLS->setCurrentIndex(styleMap.count(pen.style()) ? styleMap.at(pen.style()) : 2); });
            guard(spnLW, [&]{ spnLW->setValue(pen.width()); });
            guard(cmbLC, [&]{
                int ci = -1;
                for (int i = 0; i < cmbLC->count(); ++i) {
                    QColor c = cmbLC->itemData(i, Qt::UserRole).value<QColor>();
                    if (c == pen.color()) { ci = i; break; }
                }
                if (ci >= 0) cmbLC->setCurrentIndex(ci);
            });
        }

        QCPScatterStyle ss = graph->scatterStyle();
        // 散点形状映射
        static const std::map<QCPScatterStyle::ScatterShape, int> shapeMap = {
            {QCPScatterStyle::ssNone, 0}, {QCPScatterStyle::ssDisc, 1}, {QCPScatterStyle::ssCircle, 2},
            {QCPScatterStyle::ssSquare, 3}, {QCPScatterStyle::ssDiamond, 4}, {QCPScatterStyle::ssStar, 5}
        };
        guard(cmbSC, [&]{ cmbSC->setCurrentIndex(shapeMap.count(ss.shape()) ? shapeMap.at(ss.shape()) : 0); });
        guard(spnSS, [&]{ spnSS->setValue(static_cast<int>(ss.size())); });

        guard(cmbSCo, [&]{
            QColor targetColor = ss.pen().color();
            if (ss.shape() == QCPScatterStyle::ssNone)
                targetColor = graph->pen().color();
            int ci = -1;
            for (int i = 0; i < cmbSCo->count(); ++i) {
                QColor c = cmbSCo->itemData(i, Qt::UserRole).value<QColor>();
                if (c == targetColor) { ci = i; break; }
            }
            if (ci >= 0) cmbSCo->setCurrentIndex(ci);
        });
    };

    // 清空全部图窗 → 清理内层 DockManager 中所有页面
    pm.onCleared = [this]()
    {
        logShutdownTrace(QString("pm.onCleared enter dockWidgets=%1 pageDocks=%2")
                             .arg(m_plotDockManager ? m_plotDockManager->dockWidgetsMap().size() : 0)
                             .arg(m_pageDocks.size()));
        m_rearrangingPlotLayout = true;
        clearLayoutPlaceholders();
        m_rearrangingPlotLayout = false;
        m_exprLineEdits.clear();
        m_toolbarCombos.clear();
        m_pageToolbarBaseVisible.clear();
        m_pageExprBaseVisible.clear();
        m_linkedXAxisGroups.clear();
        m_syncingLinkedXAxis = false;
        m_syncingPlotRemoval = true;

        // 先拷贝 QADS 内部 map 的 entries，再逐个移除。
        // removeDockWidget 会修改原 map，必须先拷贝避免迭代器失效。
        const auto dwMap = m_plotDockManager->dockWidgetsMap();
        for (auto it = dwMap.begin(); it != dwMap.end(); ++it)
        {
            auto* dw = it.value();
            int idx = m_pageDocks.key(dw, -1);
            if (idx >= 0)
            {
                logShutdownTrace(QString("pm.onCleared removePlotPageDock idx=%1 dock=%2")
                                     .arg(idx)
                                     .arg(reinterpret_cast<quintptr>(dw), 0, 16));
                removePlotPageDock(idx);
            }
        }

        m_syncingPlotRemoval = false;
        m_savedTabbedPlotLayoutState.clear();
        m_hasSavedTabbedPlotLayoutState = false;
        logShutdownTrace("pm.onCleared leave");
    };

    pm.onLayoutModeChanged = [this](viewer::PlotLayoutMode mode)
    {
        applyPlotLayoutMode(mode);
    };
}
