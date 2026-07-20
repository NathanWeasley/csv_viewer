#include "UI.h"
#include "FFTDialog.h"
#include "STFTDialog.h"
#include "code_viewer/datamgr/math/fft_core.h"
#include "code_viewer/datamgr/math/stft_core.h"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <algorithm>
#include <cmath>

extern bool isSystemInDark();

namespace
{

double estimateSampleFrequencyHz(const viewer::DataManager& dm,
                                 const viewer::PlotManager& pm,
                                 int pageIndex)
{
    size_t xIdx = pm.xAxisColumn(pageIndex);
    const viewer::Column* xCol = nullptr;
    if (xIdx != static_cast<size_t>(-1))
        xCol = dm.GetColumn(xIdx);
    if (!xCol)
        xCol = dm.GetIndexColumn();
    if (!xCol || xCol->size() < 2)
        return 1.0;

    const double unitScale = viewer::timeUnitToSeconds(dm.GetXAxisUnit());
    double deltaSum = 0.0;
    size_t deltaCount = 0;
    double prev = (*xCol)[0];
    for (size_t i = 1; i < xCol->size(); ++i)
    {
        const double curr = (*xCol)[i];
        if (!std::isfinite(prev) || !std::isfinite(curr))
        {
            prev = curr;
            continue;
        }

        const double delta = curr - prev;
        if (delta > 0.0)
        {
            deltaSum += delta;
            ++deltaCount;
        }
        prev = curr;
    }

    if (deltaCount == 0)
        return 1.0;

    const double avgDeltaSeconds = (deltaSum / static_cast<double>(deltaCount)) * unitScale;
    if (!(avgDeltaSeconds > 0.0) || !std::isfinite(avgDeltaSeconds))
        return 1.0;

    const double fs = 1.0 / avgDeltaSeconds;
    return (fs > 0.0 && std::isfinite(fs)) ? fs : 1.0;
}

QCPRange buildAxisRangeFromCenters(const std::vector<double>& axis)
{
    if (axis.empty())
        return QCPRange(0.0, 1.0);
    if (axis.size() == 1)
        return QCPRange(axis.front() - 0.5, axis.front() + 0.5);

    const double lowerStep = axis[1] - axis[0];
    const double upperStep = axis[axis.size() - 1] - axis[axis.size() - 2];
    const double lower = axis.front() - 0.5 * lowerStep;
    const double upper = axis.back() + 0.5 * upperStep;
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower == upper)
        return QCPRange(axis.front(), axis.back() + 1.0);
    return QCPRange(lower, upper);
}

std::vector<double> buildAlignedSTFTTimeAxis(const viewer::Column* xCol,
                                             size_t sampleCount,
                                             size_t windowSize,
                                             size_t overlap)
{
    std::vector<double> axis;
    if (!xCol || xCol->empty() || sampleCount == 0 || windowSize == 0 || overlap >= windowSize)
        return axis;

    const size_t hopSize = windowSize - overlap;
    const size_t frameCount = (sampleCount <= windowSize)
        ? 1
        : (1 + (sampleCount - windowSize + hopSize - 1) / hopSize);

    axis.reserve(frameCount);
    for (size_t frame = 0; frame < frameCount; ++frame)
    {
        const double centerIndex = static_cast<double>(frame * hopSize) + 0.5 * static_cast<double>(windowSize);
        const double clampedIndex = std::clamp(centerIndex, 0.0, static_cast<double>(sampleCount - 1));
        const size_t leftIndex = static_cast<size_t>(std::floor(clampedIndex));
        const size_t rightIndex = std::min(leftIndex + 1, sampleCount - 1);
        const double alpha = clampedIndex - static_cast<double>(leftIndex);
        const double value = (*xCol)[leftIndex] * (1.0 - alpha) + (*xCol)[rightIndex] * alpha;
        axis.push_back(value);
    }

    return axis;
}

void applyColorScaleTheme(QCPColorScale* colorScale, const viewer::PlotTheme& theme)
{
    if (!colorScale || !colorScale->axis())
        return;

    QColor axisColor = theme.axisLabelColor.toQColor();
    QColor tickColor = theme.tickLabelColor.toQColor();
    QPen basePen(theme.basePenColor.toQColor(), theme.basePenWidth);

    colorScale->axis()->setLabelColor(axisColor);
    colorScale->axis()->setTickLabelColor(tickColor);
    colorScale->axis()->setBasePen(basePen);
    colorScale->axis()->setTickPen(basePen);
    colorScale->axis()->setSubTickPen(basePen);
}

} // namespace

// ============================================================
// 用户点击右键菜单"计算FFT" → 进入框选模式
// ============================================================
void UI::onFFTRequested(int pageIndex)
{
    auto& pm = m_viewer.GetPlotManager();
    logOperationTrace(QString("FFT selection request enter page=%1 pages=%2")
                      .arg(pageIndex).arg(plotPageCount()));

    if (pageIndex < 0 || pageIndex >= plotPageCount())
        return;

    auto* container = getPlotContainer(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
        return;

    // 取消已有的框选缩放模式
    if (pm.isRectZoomActive(pageIndex))
        pm.setRectZoomActive(pageIndex, false);

    // 进入 FFT 框选模式
    m_fftSelecting = true;
    m_fftPageIndex = pageIndex;
    plot->setCursor(Qt::CrossCursor);

    // 创建半透明选择矩形（锚点初始化为当前视图范围中点，避免 displots 缩放异常）
    m_fftSelectRect = new QCPItemRect(plot);
    m_fftSelectRect->topLeft->setType(QCPItemPosition::ptPlotCoords);
    m_fftSelectRect->bottomRight->setType(QCPItemPosition::ptPlotCoords);
    double xMid = (plot->xAxis->range().lower + plot->xAxis->range().upper) * 0.5;
    double yMid = (plot->yAxis->range().lower + plot->yAxis->range().upper) * 0.5;
    m_fftSelectRect->topLeft->setCoords(xMid, yMid);
    m_fftSelectRect->bottomRight->setCoords(xMid, yMid);
    m_fftSelectRect->setPen(QPen(QColor(60, 140, 255), 1, Qt::DashLine));
    m_fftSelectRect->setBrush(QColor(60, 140, 255, 40));
    m_fftSelectRect->setVisible(false);

    plot->replot();
    logOperationTrace(QString("FFT selection active page=%1 rect=0x%2")
                      .arg(pageIndex).arg(reinterpret_cast<quintptr>(m_fftSelectRect), 0, 16));
}

// ============================================================
// FFT 框选取消（由 eventFilter 中的右键/Escape 触发）
// ============================================================
void UI::cancelFFTSelection()
{
    logOperationTrace(QString("FFT selection cancel page=%1 rect=0x%2")
                      .arg(m_fftPageIndex).arg(reinterpret_cast<quintptr>(m_fftSelectRect), 0, 16));
    m_fftSelecting = false;

    if (m_fftPageIndex >= 0 && m_fftPageIndex < plotPageCount())
    {
        auto* container = getPlotContainer(m_fftPageIndex);
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (plot && m_fftSelectRect)
        {
            plot->removeItem(m_fftSelectRect);
            plot->setCursor(Qt::ArrowCursor);
            plot->replot();
        }
    }

    m_fftSelectRect = nullptr;
    m_fftPageIndex = -1;
}

// ============================================================
// 解析框选完成 → 显示 FFT 参数对话框 → 启动 FFT 计算
// ============================================================
void UI::showFFTDialog(int pageIndex, double xMin, double xMax)
{
    auto& dm = m_viewer.GetDataManager();
    auto& pm = m_viewer.GetPlotManager();
    logOperationTrace(QString("FFT dialog enter page=%1 xMin=%2 xMax=%3 pages=%4")
                      .arg(pageIndex).arg(xMin, 0, 'g', 16).arg(xMax, 0, 'g', 16)
                      .arg(pm.pageCount()));

    if (pageIndex < 0 || pageIndex >= pm.pageCount())
        return;

    auto resolveFFTSourceColumn = [&dm, &pm, pageIndex](const std::string& itemName) -> const viewer::Column*
    {
        viewer::PlotExpression* pe = pm.pageInfo(pageIndex).exprMgr.get(itemName);
        if (pe && pe->computedData)
            return pe->computedData.get();

        return dm.GetColumn(itemName);
    };

    // 获取当前选中的数据项
    std::string selItem = pm.selectedDataItem(pageIndex);
    if (selItem.empty())
    {
        const auto& items = pm.pageInfo(pageIndex).dataItems;
        if (items.empty())
        {
            QMessageBox::warning(this, "错误", "图窗中没有数据项。");
            return;
        }
        selItem = *items.begin();
    }

    const viewer::Column* srcCol = resolveFFTSourceColumn(selItem);
    if (!srcCol)
    {
        QMessageBox::warning(this, "错误",
            QString("无法获取数据项 '%1'。").arg(QString::fromStdString(selItem)));
        return;
    }

    // 获取 X 轴列
    size_t xIdx = pm.xAxisColumn(pageIndex);
    const viewer::Column* xCol = nullptr;
    if (xIdx != static_cast<size_t>(-1))
        xCol = dm.GetColumn(xIdx);
    if (!xCol)
    {
        dm.ensureIndexColumnBuilt();
        xCol = dm.GetIndexColumn();
    }
    if (!xCol)
        return;

    size_t rowCount = srcCol->size();
    if (xCol->size() != rowCount)
        return;

    // 查找 xMin~xMax 范围内的数据行
    size_t startIdx = 0, endIdx = 0;
    bool found = false;
    for (size_t i = 0; i < rowCount; ++i)
    {
        double xv = (*xCol)[i];
        if (xv >= xMin && xv <= xMax)
        {
            if (!found) { startIdx = i; found = true; }
            endIdx = i;
        }
    }
    if (!found || startIdx >= rowCount)
    {
        QMessageBox::warning(this, "错误", "框选范围内没有有效数据，请重新框选。");
        return;
    }

    size_t dataCount = endIdx - startIdx + 1;

    // 收集数据项列表
    const auto& dataItems = pm.pageInfo(pageIndex).dataItems;
    std::vector<std::string> itemList(dataItems.begin(), dataItems.end());

    // 显示对话框（传入当前 X 轴单位作为采样间隔默认单位）
    viewer::TimeUnit xUnit = dm.GetXAxisUnit();
    FFTDialog dlg(itemList, selItem, dataCount, xUnit, this);
    if (dlg.exec() != QDialog::Accepted)
    {
        logOperationTrace(QString("FFT dialog cancelled page=%1").arg(pageIndex));
        return;
    }

    std::string chosenItem = dlg.selectedDataItem();
    double sampleInterval = dlg.sampleInterval();
    size_t fftN = dlg.fftSize();
    logOperationTrace(QString("FFT parameters page=%1 item=\"%2\" samplesInRange=%3 fftSize=%4 sampleInterval=%5")
                      .arg(pageIndex).arg(QString::fromStdString(chosenItem))
                      .arg(dataCount).arg(fftN).arg(sampleInterval, 0, 'g', 16));

    if (chosenItem != selItem)
    {
        srcCol = resolveFFTSourceColumn(chosenItem);
        if (!srcCol) return;
    }

    // ---- 创建 FFT 图窗 ----
    int fftPageIdx = pm.addFFTPage("FFT: " + chosenItem);
    QPointer<QWidget> fftContainer = getPlotContainer(fftPageIdx);
    logOperationTrace(QString("FFT output page created sourcePage=%1 outputPage=%2 container=0x%3")
                      .arg(pageIndex).arg(fftPageIdx)
                      .arg(reinterpret_cast<quintptr>(fftContainer.data()), 0, 16));

    // ---- 准备两列数据 ----
    auto realCol = std::make_unique<viewer::Column>();
    auto imagCol = std::make_unique<viewer::Column>();
    realCol->reserve(fftN);
    imagCol->reserve(fftN);

    size_t copyCount = (dataCount > fftN) ? fftN : dataCount;
    for (size_t i = 0; i < copyCount; ++i)
        realCol->push_back((*srcCol)[startIdx + i]);
    for (size_t i = copyCount; i < fftN; ++i)
        realCol->push_back(0.0);
    for (size_t i = 0; i < fftN; ++i)
        imagCol->push_back(0.0);

    viewer::Column* realPtr = realCol.get();
    viewer::Column* imagPtr = imagCol.get();

    // ---- 启动 FFT 线程 ----
    auto* fftMgr = new viewer::FFTManager(this);

    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);

    connect(fftMgr, &viewer::FFTManager::progressChanged, this,
        [this](float progress)
        {
            m_progressBar->setValue(static_cast<int>(progress * 100.0f));
        });

    connect(fftMgr, &viewer::FFTManager::finished, this,
        [this, fftMgr, fftContainer,
         realCol = std::move(realCol), imagCol = std::move(imagCol)]() mutable
        {
            logOperationTrace(QString("FFT finished signal manager=0x%1 containerValid=%2")
                              .arg(reinterpret_cast<quintptr>(fftMgr), 0, 16).arg(!fftContainer.isNull()));
            m_progressBar->setVisible(false);

            if (!fftContainer)
            {
                fftMgr->deleteLater();
                return;
            }

            int fftPageIdx = -1;
            for (auto it = m_pageDocks.begin(); it != m_pageDocks.end(); ++it)
            {
                if (it.value() && it.value()->widget() == fftContainer.data())
                {
                    fftPageIdx = it.key();
                    break;
                }
            }
            if (fftPageIdx < 0 || fftPageIdx >= plotPageCount())
            {
                fftMgr->deleteLater();
                return;
            }

            auto* container = fftContainer.data();
            auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
            if (!plot)
            {
                fftMgr->deleteLater();
                return;
            }

            // 持有列生命周期
            viewer::Column* magPtr = realCol.get();
            viewer::Column* freqPtr = imagCol.get();

            // FFT 通过裸指针原地修改了数据，Column 的 min/max 缓存已过期
            magPtr->recalcMinMax();
            freqPtr->recalcMinMax();

            // 创建 graph：key=频率列, data=幅值列
            auto* graph = new viewer::QCPColumnGraph(plot->xAxis, plot->yAxis);
            graph->setName("FFT Spectrum");
            graph->setDataColumns(freqPtr, magPtr);
            graph->setPen(QPen(QColor(60, 140, 255), 1));

            // ---- 向工具栏注册 FFT 数据项，启用样式编辑 ----
            {
                auto* vbox = container->findChild<QVBoxLayout*>();
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
                                cmbDataItem->blockSignals(true);
                                cmbDataItem->addItem("FFT Spectrum");
                                cmbDataItem->setItemData(0, "FFT Spectrum", Qt::UserRole);
                                cmbDataItem->setCurrentIndex(0);
                                cmbDataItem->blockSignals(false);

                                m_toolbarCombos[fftPageIdx] = cmbDataItem;

                                // 触发 onSelectedDataItemChanged → 加载 graph 样式到工具栏控件
                                auto& pm = m_viewer.GetPlotManager();
                                pm.setSelectedDataItem(fftPageIdx, "FFT Spectrum");

                                // 启用删除按钮（占位，FFT 不支持删除单曲线）
                                auto* btnDelete = qobject_cast<QPushButton*>(hb->itemAt(10)->widget());
                                if (btnDelete)
                                    btnDelete->setEnabled(false);  // FFT 图窗禁止删除
                            }
                        }
                    }

                    // 隐藏表达式编辑栏（FFT 数据不支持表达式）
                    if (vbox->count() >= 3)
                    {
                        auto* exprBar = qobject_cast<QWidget*>(vbox->itemAt(2)->widget());
                        if (exprBar)
                            exprBar->setVisible(false);
                    }
                }
            }

            // 存储列生命周期
            m_fftMagCols[fftPageIdx] = std::move(realCol);
            m_fftFreqCols[fftPageIdx] = std::move(imagCol);
            setPlotPageBaseChrome(fftPageIdx, true, false);
            updatePlotPageChromeForLayout(m_viewer.GetPlotManager().layoutMode());

            plot->rescaleAxes();
            plot->replot();

            logOperationTrace(QString("FFT result installed page=%1 points=%2 graph=0x%3")
                              .arg(fftPageIdx).arg(magPtr->size())
                              .arg(reinterpret_cast<quintptr>(graph), 0, 16));

            fftMgr->deleteLater();
        });

    logOperationTrace(QString("FFT worker start page=%1 outputPage=%2 fftSize=%3 manager=0x%4")
                      .arg(pageIndex).arg(fftPageIdx).arg(fftN)
                      .arg(reinterpret_cast<quintptr>(fftMgr), 0, 16));
    fftMgr->startFFT(realPtr, imagPtr, fftN, sampleInterval, nullptr, nullptr);
}

void UI::onSTFTRequested(int pageIndex)
{
    showSTFTDialog(pageIndex);
}

void UI::showSTFTDialog(int pageIndex)
{
    auto& dm = m_viewer.GetDataManager();
    auto& pm = m_viewer.GetPlotManager();
    logOperationTrace(QString("STFT dialog enter page=%1 pages=%2")
                      .arg(pageIndex).arg(pm.pageCount()));

    if (pageIndex < 0 || pageIndex >= pm.pageCount())
        return;

    const auto& pageInfo = pm.pageInfo(pageIndex);
    if (pageInfo.dataItems.empty())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("当前图窗中没有可用于 STFT 的数据项。"));
        return;
    }

    auto resolveSTFTSourceColumn = [&dm, &pm, pageIndex](const std::string& itemName) -> const viewer::Column*
    {
        viewer::PlotExpression* pe = pm.pageInfo(pageIndex).exprMgr.get(itemName);
        if (pe && pe->computedData)
            return pe->computedData.get();

        return dm.GetColumn(itemName);
    };

    std::string selItem = pm.selectedDataItem(pageIndex);
    if (selItem.empty())
        selItem = *pageInfo.dataItems.begin();

    const viewer::Column* srcCol = resolveSTFTSourceColumn(selItem);
    if (!srcCol || srcCol->empty())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("当前选中的数据项没有可用于 STFT 的有效数据。"));
        return;
    }

    std::vector<std::string> itemList(pageInfo.dataItems.begin(), pageInfo.dataItems.end());
    const double defaultSampleFrequency = estimateSampleFrequencyHz(dm, pm, pageIndex);

    STFTDialog dlg(itemList, selItem, srcCol->size(), defaultSampleFrequency, this);
    if (dlg.exec() != QDialog::Accepted)
    {
        logOperationTrace(QString("STFT dialog cancelled page=%1").arg(pageIndex));
        return;
    }

    const std::string chosenItem = dlg.selectedDataItem();
    srcCol = resolveSTFTSourceColumn(chosenItem);
    if (!srcCol || srcCol->empty())
    {
        QMessageBox::warning(this, QString::fromUtf8("错误"),
                             QString::fromUtf8("所选数据项没有可用于 STFT 的有效数据。"));
        return;
    }

    viewer::Column inputData(*srcCol);
    const size_t windowSize = dlg.windowSize();
    const size_t overlap = dlg.overlap();
    const size_t fftSize = dlg.fftSize();
    const double sampleFrequency = dlg.sampleFrequency();
    const viewer::STFTWindowType windowType = dlg.windowType();
    logOperationTrace(QString("STFT parameters page=%1 item=\"%2\" samples=%3 window=%4 overlap=%5 fftSize=%6 frequency=%7 windowType=%8")
                      .arg(pageIndex).arg(QString::fromStdString(chosenItem)).arg(srcCol->size())
                      .arg(windowSize).arg(overlap).arg(fftSize)
                      .arg(sampleFrequency, 0, 'g', 16).arg(static_cast<int>(windowType)));
    const size_t xIdx = pm.xAxisColumn(pageIndex);
    const viewer::Column* stftXCol = (xIdx != static_cast<size_t>(-1)) ? dm.GetColumn(xIdx) : dm.GetIndexColumn();
    const std::vector<double> alignedTimeAxis =
        buildAlignedSTFTTimeAxis(stftXCol, srcCol->size(), windowSize, overlap);

    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);

    auto* watcher = new QFutureWatcher<viewer::STFTResult>(this);
    connect(watcher, &QFutureWatcher<viewer::STFTResult>::finished, this,
        [this, watcher, pageIndex, chosenItem, alignedTimeAxis]()
        {
            logOperationTrace(QString("STFT finished signal sourcePage=%1 item=\"%2\"")
                              .arg(pageIndex).arg(QString::fromStdString(chosenItem)));
            m_progressBar->hide();
            m_progressBar->setRange(0, 1000);
            m_progressBar->setValue(0);

            viewer::STFTResult result = watcher->result();
            watcher->deleteLater();

            if (result.empty())
            {
                logOperationTrace(QString("STFT result empty sourcePage=%1").arg(pageIndex));
                QMessageBox::warning(this, QString::fromUtf8("STFT 失败"),
                                     QString::fromUtf8("STFT 计算结果为空，请检查参数设置。"));
                return;
            }

            auto& pm = m_viewer.GetPlotManager();
            m_pendingDockTargetPage = pageIndex;
            m_pendingDockArea = ads::BottomDockWidgetArea;
            const int stftPageIndex = pm.addFFTPage("STFT: " + chosenItem);
            logOperationTrace(QString("STFT output page created sourcePage=%1 outputPage=%2 timeBins=%3 freqBins=%4")
                              .arg(pageIndex).arg(stftPageIndex)
                              .arg(result.timeBinCount).arg(result.freqBinCount));

            auto* container = getPlotContainer(stftPageIndex);
            auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
            if (!plot)
                return;

            auto* colorMap = new QCPColorMap(plot->xAxis, plot->yAxis);
            if (alignedTimeAxis.size() == result.timeBinCount)
                result.timeAxis = alignedTimeAxis;

            const QCPRange timeRange = buildAxisRangeFromCenters(result.timeAxis);
            const QCPRange freqRange = buildAxisRangeFromCenters(result.freqAxis);
            colorMap->data()->setSize(static_cast<int>(result.timeBinCount),
                                      static_cast<int>(result.freqBinCount));
            colorMap->data()->setRange(timeRange, freqRange);

            for (int freqIndex = 0; freqIndex < static_cast<int>(result.freqBinCount); ++freqIndex)
            {
                for (int timeIndex = 0; timeIndex < static_cast<int>(result.timeBinCount); ++timeIndex)
                {
                    colorMap->data()->setCell(
                        timeIndex,
                        freqIndex,
                        result.magnitudeDb[static_cast<size_t>(freqIndex) * result.timeBinCount +
                                           static_cast<size_t>(timeIndex)]);
                }
            }

            auto* colorScale = new QCPColorScale(plot);
            plot->plotLayout()->addElement(0, 1, colorScale);
            colorScale->setType(QCPAxis::atRight);
            colorScale->axis()->setLabel(QString::fromUtf8("幅值 (dB)"));

            colorMap->setColorScale(colorScale);
            colorMap->setGradient(QCPColorGradient::gpJet);
            colorMap->setInterpolate(false);
            colorMap->rescaleDataRange();

            plot->xAxis->setLabel(QString::fromUtf8("时间 (s)"));
            plot->yAxis->setLabel(QString::fromUtf8("频率 (Hz)"));
            plot->xAxis->setRange(timeRange);
            plot->yAxis->setRange(freqRange);

            const auto& theme = m_viewer.GetStyleManager().plotTheme(isSystemInDark());
            applyColorScaleTheme(colorScale, theme);

            if (auto* vbox = container->findChild<QVBoxLayout*>())
            {
                if (vbox->count() >= 1)
                {
                    if (auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget()))
                        toolbar->setVisible(false);
                }
                if (vbox->count() >= 3)
                {
                    if (auto* exprBar = qobject_cast<QWidget*>(vbox->itemAt(2)->widget()))
                        exprBar->setVisible(false);
                }
            }

            setPlotPageBaseChrome(stftPageIndex, false, false);
            updatePlotPageChromeForLayout(m_viewer.GetPlotManager().layoutMode());

            // STFT 图窗创建后自动加入数据图窗的 X 轴联动组。
            const int sourceGroupIndex = linkedXAxisGroupIndexForPage(pageIndex);
            if (sourceGroupIndex >= 0 && sourceGroupIndex < m_linkedXAxisGroups.size())
            {
                if (!m_linkedXAxisGroups[sourceGroupIndex].contains(stftPageIndex))
                    m_linkedXAxisGroups[sourceGroupIndex].append(stftPageIndex);
            }
            else
            {
                m_linkedXAxisGroups.append(QList<int>{ pageIndex, stftPageIndex });
            }
            cleanupLinkedXAxisGroups();

            if (auto* sourcePlot = getPlot(pageIndex))
                syncLinkedXAxisRange(pageIndex, sourcePlot->xAxis->range());

            plot->replot();
            logOperationTrace(QString("STFT result installed sourcePage=%1 outputPage=%2 linkGroups=%3")
                              .arg(pageIndex).arg(stftPageIndex).arg(m_linkedXAxisGroups.size()));
        });

    logOperationTrace(QString("STFT worker start page=%1 item=\"%2\"")
                      .arg(pageIndex).arg(QString::fromStdString(chosenItem)));
    watcher->setFuture(QtConcurrent::run(
        [inputData = std::move(inputData), windowSize, overlap, fftSize, sampleFrequency, windowType]() mutable
        {
            return viewer::stftCompute(inputData, windowSize, overlap, fftSize, sampleFrequency, windowType);
        }));
}
