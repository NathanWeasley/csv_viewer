#include "FFTDialog.h"
#include "code_viewer/datamgr/math/fft_core.h"

#include <QMessageBox>

FFTDialog::FFTDialog(const std::vector<std::string>& dataItems,
                     const std::string& selectedItem,
                     size_t dataCount,
                     viewer::TimeUnit xAxisUnit,
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
    form->addRow(QString::fromUtf8("数据项:"), m_cmbDataItem);

    // 数据点数提示
    m_lblDataCount = new QLabel(QString::fromUtf8("框选范围内共 %1 个数据点 / 建议 FFT 点数 ≥ %2")
        .arg(dataCount)
        .arg(viewer::nextPowerOfTwo(dataCount)));
    m_lblDataCount->setStyleSheet("color: #888; font-size: 11px;");
    form->addRow("", m_lblDataCount);

    // 采样间隔（同行拆为：数值框 + 单位下拉）
    {
        auto* hbox = new QHBoxLayout();
        m_spnSampleInterval = new QDoubleSpinBox();
        m_spnSampleInterval->setRange(1e-9, 1e9);
        m_spnSampleInterval->setValue(1.0);
        m_spnSampleInterval->setDecimals(10);
        hbox->addWidget(m_spnSampleInterval, 1);

        m_cmbSampleUnit = new QComboBox();
        for (size_t i = 0; i < viewer::g_timeUnitLabelCount; ++i)
            m_cmbSampleUnit->addItem(QString::fromUtf8(viewer::g_timeUnitLabels[i]));
        // 默认值设为传入的 X 轴单位
        m_cmbSampleUnit->setCurrentIndex(static_cast<int>(xAxisUnit));
        hbox->addWidget(m_cmbSampleUnit);

        form->addRow(QString::fromUtf8("采样间隔:"), hbox);
    }

    // FFT 点数
    m_spnFFTSize = new QSpinBox();
    m_spnFFTSize->setRange(2, 1048576);
    size_t defaultN = viewer::nextPowerOfTwo(dataCount);
    if (defaultN < 2) defaultN = 2;
    m_spnFFTSize->setValue(static_cast<int>(defaultN));
    form->addRow(QString::fromUtf8("FFT 点数:"), m_spnFFTSize);

    mainLayout->addLayout(form);
    mainLayout->addSpacing(12);

    // 按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton(QString::fromUtf8("取消"));
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOK = new QPushButton(QString::fromUtf8("确认"));
    btnOK->setDefault(true);
    connect(btnOK, &QPushButton::clicked, this, [this]()
    {
        // 验证 FFT 点数是否为 2 的幂
        int n = m_spnFFTSize->value();
        if ((n & (n - 1)) != 0)
        {
            QMessageBox::warning(this, QString::fromUtf8("参数错误"),
                QString::fromUtf8("FFT 点数必须是 2 的幂（如 2, 4, 8, 16, ...）。"));
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
    double rawValue = m_spnSampleInterval->value();
    // 根据单位下拉框自动转换为秒
    if (m_cmbSampleUnit)
    {
        viewer::TimeUnit unit = static_cast<viewer::TimeUnit>(m_cmbSampleUnit->currentIndex());
        rawValue *= viewer::timeUnitToSeconds(unit);
    }
    return rawValue;
}

size_t FFTDialog::fftSize() const
{
    return static_cast<size_t>(m_spnFFTSize->value());
}
