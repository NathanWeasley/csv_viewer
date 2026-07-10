#pragma once

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>

#include <string>
#include <vector>

#include "code_viewer/datamgr/math/stft_core.h"

class STFTDialog : public QDialog
{
    Q_OBJECT

public:
    STFTDialog(const std::vector<std::string>& dataItems,
               const std::string& selectedItem,
               size_t dataCount,
               double defaultSampleFrequency,
               QWidget* parent = nullptr);

    std::string selectedDataItem() const;
    size_t windowSize() const;
    size_t overlap() const;
    size_t fftSize() const;
    double sampleFrequency() const;
    viewer::STFTWindowType windowType() const;

private:
    QComboBox* m_cmbDataItem = nullptr;
    QSpinBox* m_spnWindowSize = nullptr;
    QSpinBox* m_spnOverlap = nullptr;
    QSpinBox* m_spnFFTSize = nullptr;
    QDoubleSpinBox* m_spnSampleFrequency = nullptr;
    QComboBox* m_cmbWindowType = nullptr;
    QLabel* m_lblDataCount = nullptr;
};
