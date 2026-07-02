#include "UI.h"
#include "FFTDialog.h"
#include "code_viewer/datamgr/math/fft_core.h"

#include <QMessageBox>

// ============================================================
// 用户点击右键菜单"计算FFT" → 进入框选模式
// ============================================================
void UI::onFFTRequested(int pageIndex)
{
    auto& pm = m_viewer.GetPlotManager();

    if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
        return;

    auto* container = m_plotTabs->widget(pageIndex);
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
}

// ============================================================
// FFT 框选取消（由 eventFilter 中的右键/Escape 触发）
// ============================================================
void UI::cancelFFTSelection()
{
    m_fftSelecting = false;

    if (m_fftPageIndex >= 0 && m_fftPageIndex < m_plotTabs->count())
    {
        auto* container = m_plotTabs->widget(m_fftPageIndex);
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

    const viewer::Column* srcCol = dm.GetColumn(selItem);
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
        return;

    std::string chosenItem = dlg.selectedDataItem();
    double sampleInterval = dlg.sampleInterval();
    size_t fftN = dlg.fftSize();

    if (chosenItem != selItem)
    {
        srcCol = dm.GetColumn(chosenItem);
        if (!srcCol) return;
    }

    // ---- 创建 FFT 图窗 ----
    int fftPageIdx = pm.addFFTPage("FFT: " + chosenItem);

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
        [this, fftMgr, fftPageIdx,
         realCol = std::move(realCol), imagCol = std::move(imagCol)]() mutable
        {
            m_progressBar->setVisible(false);

            if (fftPageIdx < 0 || fftPageIdx >= m_plotTabs->count())
            {
                fftMgr->deleteLater();
                return;
            }

            auto* container = m_plotTabs->widget(fftPageIdx);
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

            plot->rescaleAxes();
            plot->replot();

            fftMgr->deleteLater();
        });

    fftMgr->startFFT(realPtr, imagPtr, fftN, sampleInterval, nullptr, nullptr);
}