#include "UI.h"
#include "FFTDialog.h"
#include "STFTDialog.h"
#include "code_viewer/datamgr/math/stft_core.h"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_set>

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

struct FFTBatchItem
{
    std::string sourceName;
    QColor sourceColor;
    std::unique_ptr<viewer::Column> sourceColumn;
    std::unique_ptr<viewer::Column> spectrumColumn;
};

struct FFTBatchState
{
    QPointer<QWidget> outputContainer;
    std::vector<FFTBatchItem> items;
    std::unique_ptr<viewer::Column> frequencyColumn;
    size_t nextItem = 0;
    size_t startIndex = 0;
    size_t sampleCount = 0;
    size_t fftSize = 0;
    double sampleInterval = 1.0;
    bool removeBaseline = false;
    bool calculatePowerSpectrum = false;
};

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
        if (pe && pe->isEdited && pe->computedData)
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

    // 优先恢复本次启动期间上次确认过的参数；已记忆数据项不可用时使用当前项。
    viewer::TimeUnit xUnit = dm.GetXAxisUnit();
    const bool rememberedAllDataItems = m_fftParameterMemory.valid
        && m_fftParameterMemory.allDataItems;
    const bool rememberedItemAvailable = m_fftParameterMemory.valid
        && !rememberedAllDataItems
        && std::find(itemList.begin(), itemList.end(),
                     m_fftParameterMemory.dataItem) != itemList.end();
    const std::string initialItem = rememberedItemAvailable
        ? m_fftParameterMemory.dataItem : selItem;
    FFTDialog dlg(itemList, initialItem, dataCount, xUnit, this);
    dlg.setAllDataItemsSelected(rememberedAllDataItems);
    if (m_fftParameterMemory.valid)
    {
        dlg.setRememberedParameters(
            m_fftParameterMemory.sampleIntervalValue,
            m_fftParameterMemory.sampleUnit,
            m_fftParameterMemory.fftSize,
            m_fftParameterMemory.removeBaseline,
            m_fftParameterMemory.calculatePowerSpectrum);
    }
    if (dlg.exec() != QDialog::Accepted)
    {
        logOperationTrace(QString("FFT dialog cancelled page=%1").arg(pageIndex));
        return;
    }

    const bool allDataItems = dlg.allDataItemsSelected();
    const std::string chosenItem = dlg.selectedDataItem();
    double sampleInterval = dlg.sampleInterval();
    size_t fftN = dlg.fftSize();
    const bool removeBaseline = dlg.removeBaseline();
    const bool calculatePowerSpectrum = dlg.calculatePowerSpectrum();
    m_fftParameterMemory.valid = true;
    m_fftParameterMemory.dataItem = chosenItem;
    m_fftParameterMemory.allDataItems = allDataItems;
    m_fftParameterMemory.sampleIntervalValue = dlg.sampleIntervalInputValue();
    m_fftParameterMemory.sampleUnit = dlg.sampleIntervalUnit();
    m_fftParameterMemory.fftSize = fftN;
    m_fftParameterMemory.removeBaseline = removeBaseline;
    m_fftParameterMemory.calculatePowerSpectrum = calculatePowerSpectrum;
    const QString selectionLabel = allDataItems
        ? QString::fromUtf8("全部已加载数据")
        : QString::fromStdString(chosenItem);
    logOperationTrace(QString("FFT parameters page=%1 item=\"%2\" allItems=%3 samplesInRange=%4 fftSize=%5 sampleInterval=%6 linearDetrend=%7 powerSpectrum=%8")
                      .arg(pageIndex).arg(selectionLabel)
                      .arg(allDataItems ? "true" : "false")
                      .arg(dataCount).arg(fftN).arg(sampleInterval, 0, 'g', 16)
                      .arg(removeBaseline ? "true" : "false")
                      .arg(calculatePowerSpectrum ? "true" : "false"));

    // Preserve the source plot order for batch results so the result selector
    // follows the same order the user sees in the source window.
    auto* sourcePlot = getPlot(pageIndex);
    if (!sourcePlot)
        return;

    std::vector<std::string> sourceNames;
    if (allDataItems)
    {
        std::unordered_set<std::string> appendedNames;
        for (int index = 0; index < sourcePlot->plottableCount(); ++index)
        {
            auto* plottable = sourcePlot->plottable(index);
            if (!plottable)
                continue;
            const std::string name = plottable->name().toStdString();
            if (dataItems.count(name) > 0 && appendedNames.insert(name).second)
                sourceNames.push_back(name);
        }
        for (const std::string& name : itemList)
        {
            if (appendedNames.insert(name).second)
                sourceNames.push_back(name);
        }
    }
    else if (!chosenItem.empty())
    {
        sourceNames.push_back(chosenItem);
    }

    if (sourceNames.empty())
    {
        QMessageBox::warning(this, QString::fromUtf8("FFT 失败"),
                             QString::fromUtf8("没有可用于 FFT 的已加载数据。"));
        return;
    }

    auto state = std::make_shared<FFTBatchState>();
    state->startIndex = 0;
    state->sampleCount = dataCount;
    state->fftSize = fftN;
    state->sampleInterval = sampleInterval;
    state->removeBaseline = removeBaseline;
    state->calculatePowerSpectrum = calculatePowerSpectrum;
    state->frequencyColumn = std::make_unique<viewer::Column>(fftN);
    state->items.reserve(sourceNames.size());

    for (const std::string& name : sourceNames)
    {
        const viewer::Column* sourceColumn = resolveFFTSourceColumn(name);
        if (!sourceColumn || endIdx >= sourceColumn->size())
        {
            QMessageBox::warning(
                this, QString::fromUtf8("FFT 失败"),
                QString::fromUtf8("数据项“%1”在框选范围内没有完整的有效数据。")
                    .arg(QString::fromStdString(name)));
            return;
        }

        std::vector<double> selectedSamples(dataCount);
        for (size_t offset = 0; offset < dataCount; ++offset)
            selectedSamples[offset] = (*sourceColumn)[startIdx + offset];

        QColor sourceColor(60, 140, 255);
        for (int index = 0; index < sourcePlot->plottableCount(); ++index)
        {
            auto* plottable = sourcePlot->plottable(index);
            if (plottable && plottable->name().toStdString() == name)
            {
                sourceColor = plottable->pen().color();
                break;
            }
        }

        FFTBatchItem item;
        item.sourceName = name;
        item.sourceColor = sourceColor;
        item.sourceColumn = std::make_unique<viewer::Column>(
            std::move(selectedSamples));
        item.spectrumColumn = std::make_unique<viewer::Column>(fftN);
        state->items.push_back(std::move(item));
    }

    const QString outputTitle = QString::fromUtf8("FFT: %1 [%2]")
        .arg(selectionLabel)
        .arg(calculatePowerSpectrum
            ? QString::fromUtf8("功率谱 dB")
            : QString::fromUtf8("幅值谱"));
    const int outputPageIndex = pm.addFFTPage(outputTitle.toStdString());
    state->outputContainer = getPlotContainer(outputPageIndex);
    logOperationTrace(QString("FFT output page created sourcePage=%1 outputPage=%2 items=%3 container=0x%4")
                      .arg(pageIndex).arg(outputPageIndex).arg(state->items.size())
                      .arg(reinterpret_cast<quintptr>(state->outputContainer.data()), 0, 16));

    auto installResults = [this, state]() mutable
    {
        m_progressBar->setVisible(false);
        if (!state->outputContainer)
            return;

        int fftPageIndex = -1;
        for (auto it = m_pageDocks.begin(); it != m_pageDocks.end(); ++it)
        {
            if (it.value()
                && it.value()->widget() == state->outputContainer.data())
            {
                fftPageIndex = it.key();
                break;
            }
        }
        if (fftPageIndex < 0 || fftPageIndex >= plotPageCount())
            return;

        auto* container = state->outputContainer.data();
        auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
        if (!plot || !state->frequencyColumn)
            return;

        viewer::Column* frequencyColumn = state->frequencyColumn.get();
        std::vector<std::unique_ptr<viewer::Column>> spectrumColumns;
        spectrumColumns.reserve(state->items.size());

        for (auto& item : state->items)
        {
            viewer::Column* spectrumColumn = item.spectrumColumn.get();
            auto* graph = new viewer::QCPColumnGraph(plot->xAxis, plot->yAxis);
            graph->setName(QString::fromStdString(item.sourceName));
            graph->setDataColumns(frequencyColumn, spectrumColumn);
            graph->setPen(QPen(item.sourceColor, 1));
            spectrumColumns.push_back(std::move(item.spectrumColumn));
        }

        plot->xAxis->setLabel(QString::fromUtf8("频率 (Hz)"));
        plot->yAxis->setLabel(state->calculatePowerSpectrum
            ? QString::fromUtf8("功率谱 (dB)")
            : QString::fromUtf8("幅值"));

        if (auto* vbox = container->findChild<QVBoxLayout*>())
        {
            if (vbox->count() >= 1)
            {
                if (auto* toolbar = qobject_cast<QWidget*>(vbox->itemAt(0)->widget()))
                {
                    if (auto* hb = toolbar->findChild<QHBoxLayout*>())
                    {
                        if (hb->count() >= 11)
                        {
                            if (auto* combo = qobject_cast<QComboBox*>(hb->itemAt(0)->widget()))
                            {
                                combo->blockSignals(true);
                                for (const auto& item : state->items)
                                {
                                    const QString name =
                                        QString::fromStdString(item.sourceName);
                                    combo->addItem(name);
                                    combo->setItemData(
                                        combo->count() - 1, name, Qt::UserRole);
                                }
                                combo->setCurrentIndex(0);
                                combo->blockSignals(false);
                                m_toolbarCombos[fftPageIndex] = combo;
                                m_viewer.GetPlotManager().setSelectedDataItem(
                                    fftPageIndex, state->items.front().sourceName);
                            }

                            if (auto* deleteButton = qobject_cast<QPushButton*>(
                                    hb->itemAt(10)->widget()))
                            {
                                deleteButton->setEnabled(false);
                            }
                        }
                    }
                }
            }
            if (vbox->count() >= 3)
            {
                if (auto* expressionBar = qobject_cast<QWidget*>(
                        vbox->itemAt(2)->widget()))
                {
                    expressionBar->setVisible(false);
                }
            }
        }

        m_fftMagCols[fftPageIndex] = std::move(spectrumColumns);
        m_fftFreqCols[fftPageIndex] = std::move(state->frequencyColumn);
        setPlotPageBaseChrome(fftPageIndex, true, false);
        updatePlotPageChromeForLayout(m_viewer.GetPlotManager().layoutMode());
        plot->rescaleAxes();
        plot->replot();

        logOperationTrace(QString("FFT batch result installed page=%1 items=%2 pointsPerItem=%3")
                          .arg(fftPageIndex).arg(state->items.size())
                          .arg(frequencyColumn->size()));
    };

    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);

    auto startNext = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakStartNext = startNext;
    *startNext = [this, state, installResults, weakStartNext]() mutable
    {
        if (!state->outputContainer)
        {
            m_progressBar->setVisible(false);
            return;
        }
        if (state->nextItem >= state->items.size())
        {
            installResults();
            return;
        }

        const auto continuation = weakStartNext.lock();
        if (!continuation)
            return;

        const size_t itemIndex = state->nextItem;
        FFTBatchItem& item = state->items[itemIndex];
        auto* fftManager = new viewer::FFTManager(this);
        connect(fftManager, &viewer::FFTManager::progressChanged, this,
            [this, state, itemIndex](float itemProgress)
            {
                const double totalProgress =
                    (static_cast<double>(itemIndex) + itemProgress)
                    / static_cast<double>(state->items.size());
                m_progressBar->setValue(static_cast<int>(totalProgress * 1000.0));
            });
        connect(fftManager, &viewer::FFTManager::finished, this,
            [this, state, fftManager, continuation]()
            {
                logOperationTrace(QString("FFT batch item finished item=%1 total=%2 manager=0x%3")
                                  .arg(state->nextItem + 1).arg(state->items.size())
                                  .arg(reinterpret_cast<quintptr>(fftManager), 0, 16));
                ++state->nextItem;
                fftManager->deleteLater();
                (*continuation)();
            });

        logOperationTrace(QString("FFT batch item start item=%1 total=%2 name=\"%3\" manager=0x%4")
                          .arg(itemIndex + 1).arg(state->items.size())
                          .arg(QString::fromStdString(item.sourceName))
                          .arg(reinterpret_cast<quintptr>(fftManager), 0, 16));
        fftManager->startFFT(
            item.sourceColumn.get(), state->startIndex, state->sampleCount,
            item.spectrumColumn.get(), state->frequencyColumn.get(),
            state->fftSize, state->sampleInterval, state->removeBaseline,
            state->calculatePowerSpectrum, nullptr, nullptr);
    };
    (*startNext)();
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
        if (pe && pe->isEdited && pe->computedData)
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

    const bool rememberedItemAvailable = m_stftParameterMemory.valid
        && std::find(itemList.begin(), itemList.end(),
                     m_stftParameterMemory.dataItem) != itemList.end();
    const std::string initialItem = rememberedItemAvailable
        ? m_stftParameterMemory.dataItem : selItem;
    STFTDialog dlg(itemList, initialItem, srcCol->size(), defaultSampleFrequency, this);
    if (m_stftParameterMemory.valid)
    {
        dlg.setRememberedParameters(
            m_stftParameterMemory.windowSize,
            m_stftParameterMemory.overlap,
            m_stftParameterMemory.fftSize,
            m_stftParameterMemory.sampleFrequency,
            m_stftParameterMemory.windowType,
            m_stftParameterMemory.removeBaseline,
            m_stftParameterMemory.highPassCutoffFrequency,
            m_stftParameterMemory.calculatePowerSpectrum);
    }
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
    const bool removeBaseline = dlg.removeBaseline();
    const double highPassCutoffFrequency = dlg.highPassCutoffFrequency();
    const bool calculatePowerSpectrum = dlg.calculatePowerSpectrum();
    m_stftParameterMemory.valid = true;
    m_stftParameterMemory.dataItem = chosenItem;
    m_stftParameterMemory.windowSize = windowSize;
    m_stftParameterMemory.overlap = overlap;
    m_stftParameterMemory.fftSize = fftSize;
    m_stftParameterMemory.sampleFrequency = sampleFrequency;
    m_stftParameterMemory.windowType = windowType;
    m_stftParameterMemory.removeBaseline = removeBaseline;
    m_stftParameterMemory.highPassCutoffFrequency = highPassCutoffFrequency;
    m_stftParameterMemory.calculatePowerSpectrum = calculatePowerSpectrum;
    logOperationTrace(QString("STFT parameters page=%1 item=\"%2\" samples=%3 window=%4 overlap=%5 fftSize=%6 frequency=%7 windowType=%8 removeBaseline=%9 highPassCutoff=%10 powerSpectrum=%11")
                      .arg(pageIndex).arg(QString::fromStdString(chosenItem)).arg(srcCol->size())
                      .arg(windowSize).arg(overlap).arg(fftSize)
                      .arg(sampleFrequency, 0, 'g', 16).arg(static_cast<int>(windowType))
                      .arg(removeBaseline ? "true" : "false")
                      .arg(highPassCutoffFrequency, 0, 'g', 16)
                      .arg(calculatePowerSpectrum ? "true" : "false"));
    const size_t xIdx = pm.xAxisColumn(pageIndex);
    const bool sourceUsesIndex = pm.usesIndexXAxis(pageIndex);
    const size_t sourceXAxisColumn = pm.selectedXAxisColumn(pageIndex);
    const viewer::Column* stftXCol = (xIdx != static_cast<size_t>(-1)) ? dm.GetColumn(xIdx) : dm.GetIndexColumn();
    const std::vector<double> alignedTimeAxis =
        buildAlignedSTFTTimeAxis(stftXCol, srcCol->size(), windowSize, overlap);

    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);

    auto* watcher = new QFutureWatcher<viewer::STFTResult>(this);
    connect(watcher, &QFutureWatcher<viewer::STFTResult>::finished, this,
        [this, watcher, pageIndex, chosenItem, alignedTimeAxis,
         sourceUsesIndex, sourceXAxisColumn]()
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
            const int stftPageIndex = pm.addFFTPage(
                "STFT: " + chosenItem
                + (result.powerSpectrum ? " [Power dB]" : " [Amplitude]"));
            pm.setXAxisState(stftPageIndex, sourceUsesIndex, sourceXAxisColumn);
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
                        result.spectrumValues[static_cast<size_t>(freqIndex) * result.timeBinCount +
                                              static_cast<size_t>(timeIndex)]);
                }
            }

            colorMap->setGradient(QCPColorGradient::gpJet);
            colorMap->setInterpolate(false);
            colorMap->rescaleDataRange();

            plot->xAxis->setLabel(QString::fromUtf8("时间 (s)"));
            plot->yAxis->setLabel(QString::fromUtf8("频率 (Hz)"));
            plot->xAxis->setRange(timeRange);
            plot->yAxis->setRange(freqRange);

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
        [inputData = std::move(inputData), windowSize, overlap, fftSize, sampleFrequency,
         windowType, removeBaseline, highPassCutoffFrequency,
         calculatePowerSpectrum]() mutable
        {
            return viewer::stftCompute(inputData, windowSize, overlap, fftSize,
                                       sampleFrequency, windowType, removeBaseline,
                                       highPassCutoffFrequency,
                                       calculatePowerSpectrum);
        }));
}
