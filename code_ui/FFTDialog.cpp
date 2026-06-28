#include "FFTDialog.h"
#include "code_viewer/datamgr/math/fft_core.h"

#include <QMessageBox>

FFTDialog::FFTDialog(const std::vector<std::string>& dataItems,
                     const std::string& selectedItem,
                     size_t dataCount,
                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("FFT 参数");
    setMinimumWidth(380);

    auto* mainLayout = new QVBoxLayout(this);

    // 表单
    auto* form = new QFormLayout();

    // 数据项下拉
    m_cmbDataItem = new QComboBox();
    int selIdx = -1;
    for (size_t i = 0; i < dataItems.size(); ++i)
    {
        QString name = QString::fromStdString(dataItems[i]);
        m_cmbDataItem->addItem(name);
        if (dataItems[i] == selectedItem)
            selIdx = static_cast<int>(i);
    }
    if (selIdx >= 0)
        m_cmbDataItem->setCurrentIndex(selIdx);
    form->addRow("数据项:", m_cmbDataItem);

    // 数据点数提示
    m_lblDataCount = new QLabel(QString("框选范围内共 %1 个数据点 / 建议 FFT 点数 ≥ %2")
        .arg(dataCount)
        .arg(viewer::nextPowerOfTwo(dataCount)));
    m_lblDataCount->setStyleSheet("color: #888; font-size: 11px;");
    form->addRow("", m_lblDataCount);

    // 采样间隔
    m_spnSampleInterval = new QDoubleSpinBox();
    m_spnSampleInterval->setRange(1e-9, 1e3);
    m_spnSampleInterval->setValue(1.0);
    m_spnSampleInterval->setDecimals(10);
    form->addRow("采样间隔（秒）:", m_spnSampleInterval);

    // FFT 点数
    m_spnFFTSize = new QSpinBox();
    m_spnFFTSize->setRange(2, 1048576);
    size_t defaultN = viewer::nextPowerOfTwo(dataCount);
    if (defaultN < 2) defaultN = 2;
    m_spnFFTSize->setValue(static_cast<int>(defaultN));
    form->addRow("FFT 点数:", m_spnFFTSize);

    mainLayout->addLayout(form);
    mainLayout->addSpacing(12);

    // 按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOK = new QPushButton("确认");
    btnOK->setDefault(true);
    connect(btnOK, &QPushButton::clicked, this, [this]()
    {
        // 验证 FFT 点数是否为 2 的幂
        int n = m_spnFFTSize->value();
        if ((n & (n - 1)) != 0)
        {
            QMessageBox::warning(this, "参数错误",
                "FFT 点数必须是 2 的幂（如 2, 4, 8, 16, ...）。");
            return;
        }
        accept();
    });
    btnLayout->addWidget(btnOK);

    mainLayout->addLayout(btnLayout);
}

std::string FFTDialog::selectedDataItem() const
{
    return m_cmbDataItem->currentText().toStdString();
}

double FFTDialog::sampleInterval() const
{
    return m_spnSampleInterval->value();
}

size_t FFTDialog::fftSize() const
{
    return static_cast<size_t>(m_spnFFTSize->value());
}