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

class FFTDialog : public QDialog
{
    Q_OBJECT

public:
    FFTDialog(const std::vector<std::string>& dataItems,
              const std::string& selectedItem,
              size_t dataCount,
              QWidget* parent = nullptr);

    std::string selectedDataItem() const;
    double sampleInterval() const;
    size_t fftSize() const;

private:
    QComboBox* m_cmbDataItem = nullptr;
    QDoubleSpinBox* m_spnSampleInterval = nullptr;
    QSpinBox* m_spnFFTSize = nullptr;
    QLabel* m_lblDataCount = nullptr;
};