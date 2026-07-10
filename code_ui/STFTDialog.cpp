#include "STFTDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

STFTDialog::STFTDialog(const std::vector<std::string>& dataItems,
                       const std::string& selectedItem,
                       size_t dataCount,
                       double defaultSampleFrequency,
                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("STFT 参数"));
    setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_cmbDataItem = new QComboBox();
    int selectedIndex = -1;
    for (size_t i = 0; i < dataItems.size(); ++i)
    {
        const QString name = QString::fromStdString(dataItems[i]);
        m_cmbDataItem->addItem(name);
        if (dataItems[i] == selectedItem)
            selectedIndex = static_cast<int>(i);
    }
    if (selectedIndex >= 0)
        m_cmbDataItem->setCurrentIndex(selectedIndex);
    form->addRow(QString::fromUtf8("数据项:"), m_cmbDataItem);

    m_lblDataCount = new QLabel(QString::fromUtf8("当前数据共 %1 个点").arg(dataCount));
    m_lblDataCount->setStyleSheet("color: #888; font-size: 11px;");
    form->addRow("", m_lblDataCount);

    const size_t suggestedWindow = std::min<size_t>(1024, viewer::nextPowerOfTwo(std::max<size_t>(64, std::min<size_t>(dataCount, 1024))));
    const size_t defaultWindow = std::max<size_t>(16, std::min<size_t>(suggestedWindow, viewer::nextPowerOfTwo(std::max<size_t>(16, std::min<size_t>(dataCount, suggestedWindow)))));
    const size_t defaultOverlap = defaultWindow / 2;
    const size_t defaultFftSize = std::max<size_t>(defaultWindow, viewer::nextPowerOfTwo(defaultWindow));

    m_spnWindowSize = new QSpinBox();
    m_spnWindowSize->setRange(16, 1048576);
    m_spnWindowSize->setValue(static_cast<int>(defaultWindow));
    form->addRow(QString::fromUtf8("窗长:"), m_spnWindowSize);

    m_spnOverlap = new QSpinBox();
    m_spnOverlap->setRange(0, static_cast<int>(defaultWindow - 1));
    m_spnOverlap->setValue(static_cast<int>(defaultOverlap));
    form->addRow(QString::fromUtf8("重叠点数:"), m_spnOverlap);

    m_spnFFTSize = new QSpinBox();
    m_spnFFTSize->setRange(16, 1048576);
    m_spnFFTSize->setValue(static_cast<int>(defaultFftSize));
    form->addRow(QString::fromUtf8("FFT 点数:"), m_spnFFTSize);

    m_spnSampleFrequency = new QDoubleSpinBox();
    m_spnSampleFrequency->setRange(1e-9, 1e12);
    m_spnSampleFrequency->setDecimals(6);
    m_spnSampleFrequency->setValue(defaultSampleFrequency > 0.0 ? defaultSampleFrequency : 1.0);
    m_spnSampleFrequency->setSuffix(" Hz");
    form->addRow(QString::fromUtf8("采样频率:"), m_spnSampleFrequency);

    m_cmbWindowType = new QComboBox();
    m_cmbWindowType->addItem(QString::fromUtf8("Hann"));
    m_cmbWindowType->addItem(QString::fromUtf8("Hamming"));
    m_cmbWindowType->addItem(QString::fromUtf8("Rectangular"));
    form->addRow(QString::fromUtf8("窗函数:"), m_cmbWindowType);

    connect(m_spnWindowSize, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value)
    {
        m_spnOverlap->setMaximum(std::max(0, value - 1));
        if (m_spnFFTSize->value() < value)
            m_spnFFTSize->setValue(static_cast<int>(viewer::nextPowerOfTwo(static_cast<size_t>(value))));
    });

    mainLayout->addLayout(form);
    mainLayout->addSpacing(12);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton(QString::fromUtf8("取消"));
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOK = new QPushButton(QString::fromUtf8("确认"));
    btnOK->setDefault(true);
    connect(btnOK, &QPushButton::clicked, this, [this]()
    {
        const int window = m_spnWindowSize->value();
        const int overlapValue = m_spnOverlap->value();
        const int fftValue = m_spnFFTSize->value();

        if (window <= 0 || overlapValue < 0 || overlapValue >= window)
        {
            QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                                 QString::fromUtf8("重叠点数必须小于窗长。"));
            return;
        }
        if ((fftValue & (fftValue - 1)) != 0)
        {
            QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                                 QString::fromUtf8("FFT 点数必须是 2 的幂。"));
            return;
        }
        if (fftValue < window)
        {
            QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                                 QString::fromUtf8("FFT 点数不能小于窗长。"));
            return;
        }
        if (m_spnSampleFrequency->value() <= 0.0)
        {
            QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                                 QString::fromUtf8("采样频率必须大于 0。"));
            return;
        }

        accept();
    });
    btnLayout->addWidget(btnOK);

    mainLayout->addLayout(btnLayout);
}

std::string STFTDialog::selectedDataItem() const
{
    return m_cmbDataItem->currentText().toStdString();
}

size_t STFTDialog::windowSize() const
{
    return static_cast<size_t>(m_spnWindowSize->value());
}

size_t STFTDialog::overlap() const
{
    return static_cast<size_t>(m_spnOverlap->value());
}

size_t STFTDialog::fftSize() const
{
    return static_cast<size_t>(m_spnFFTSize->value());
}

double STFTDialog::sampleFrequency() const
{
    return m_spnSampleFrequency->value();
}

viewer::STFTWindowType STFTDialog::windowType() const
{
    return static_cast<viewer::STFTWindowType>(m_cmbWindowType->currentIndex());
}
