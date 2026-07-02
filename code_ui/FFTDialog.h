#pragma once

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <vector>
#include <string>
#include "code_viewer/datamgr/data_manager.h"

class FFTDialog : public QDialog
{
    Q_OBJECT

public:
    // xAxisUnit: 当前图窗 X 轴单位（用于设定采样间隔下拉默认值）
    FFTDialog(const std::vector<std::string>& dataItems,
              const std::string& selectedItem,
              size_t dataCount,
              viewer::TimeUnit xAxisUnit = viewer::TimeUnit::Second,
              QWidget* parent = nullptr);

    std::string selectedDataItem() const;
    // 返回转换后的采样间隔（单位：秒）
    double sampleInterval() const;
    size_t fftSize() const;

private:
    QComboBox* m_cmbDataItem = nullptr;
    QDoubleSpinBox* m_spnSampleInterval = nullptr;
    QComboBox* m_cmbSampleUnit = nullptr;
    QSpinBox* m_spnFFTSize = nullptr;
    QLabel* m_lblDataCount = nullptr;
};
