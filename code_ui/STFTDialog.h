#pragma once

#include <QComboBox>
#include <QCheckBox>
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
    bool removeBaseline() const;
    bool calculatePowerSpectrum() const;
    double highPassCutoffFrequency() const;

    void setRememberedParameters(size_t windowSize,
                                 size_t overlap,
                                 size_t fftSize,
                                 double sampleFrequency,
                                 viewer::STFTWindowType windowType,
                                 bool removeBaseline,
                                 double highPassCutoffFrequency,
                                 bool calculatePowerSpectrum);

private:
    QComboBox* m_cmbDataItem = nullptr;
    QSpinBox* m_spnWindowSize = nullptr;
    QSpinBox* m_spnOverlap = nullptr;
    QSpinBox* m_spnFFTSize = nullptr;
    QDoubleSpinBox* m_spnSampleFrequency = nullptr;
    QComboBox* m_cmbWindowType = nullptr;
    QCheckBox* m_chkRemoveBaseline = nullptr;
    QCheckBox* m_chkCalculatePowerSpectrum = nullptr;
    QDoubleSpinBox* m_spnHighPassCutoff = nullptr;
    QLabel* m_lblDataCount = nullptr;
};
