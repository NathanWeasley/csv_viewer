#include "XAxisDialog.h"
#include <QHeaderView>

XAxisDialog::XAxisDialog(const std::vector<viewer::XAxisRule>& rules,
                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("默认X轴设置"));
    setMinimumSize(460, 360);

    auto* mainLayout = new QVBoxLayout(this);

    // ---- 表格 ----
    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("匹配模式"),
        QString::fromUtf8("单位")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(1, 100);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_table);

    // 填充现有规则
    for (const auto& rule : rules)
        addRow(rule.pattern, rule.unit);

    // 如果没有规则，添加一个空行
    if (m_table->rowCount() == 0)
        addRow("", viewer::TimeUnit::Second);

    // ---- 按钮 ----
    auto* btnLayout = new QHBoxLayout();

    m_btnAdd = new QPushButton(QString::fromUtf8("添加规则"));
    connect(m_btnAdd, &QPushButton::clicked, this, &XAxisDialog::onAddRule);
    btnLayout->addWidget(m_btnAdd);

    m_btnDelete = new QPushButton(QString::fromUtf8("删除"));
    connect(m_btnDelete, &QPushButton::clicked, this, &XAxisDialog::onDeleteRule);
    btnLayout->addWidget(m_btnDelete);

    btnLayout->addStretch();

    m_btnCancel = new QPushButton(QString::fromUtf8("取消"));
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_btnCancel);

    m_btnOK = new QPushButton(QString::fromUtf8("确认"));
    m_btnOK->setDefault(true);
    connect(m_btnOK, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_btnOK);

    mainLayout->addLayout(btnLayout);
}

void XAxisDialog::addRow(const std::string& pattern, viewer::TimeUnit unit)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);

    // 第0列：可编辑文本（匹配模式）
    {
        auto* item = new QTableWidgetItem(QString::fromStdString(pattern));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        m_table->setItem(row, 0, item);
    }

    // 第1列：单位下拉框
    {
        auto* combo = new QComboBox();
        for (size_t i = 0; i < viewer::g_timeUnitLabelCount; ++i)
            combo->addItem(QString::fromUtf8(viewer::g_timeUnitLabels[i]));
        combo->setCurrentIndex(static_cast<int>(unit));
        m_table->setCellWidget(row, 1, combo);
    }

}

void XAxisDialog::onAddRule()
{
    addRow("", viewer::TimeUnit::Second);
}

void XAxisDialog::onDeleteRule()
{
    int row = m_table->currentRow();
    if (row < 0 && m_table->rowCount() > 0)
        row = m_table->rowCount() - 1;
    if (row >= 0 && row < m_table->rowCount())
        m_table->removeRow(row);
}

std::vector<viewer::XAxisRule> XAxisDialog::rules() const
{
    std::vector<viewer::XAxisRule> result;
    for (int i = 0; i < m_table->rowCount(); ++i)
    {
        auto* patternItem = m_table->item(i, 0);
        std::string pattern = patternItem ? patternItem->text().toStdString() : "";
        if (pattern.empty())
            continue;  // 跳过空模式

        auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 1));
        viewer::TimeUnit unit = viewer::TimeUnit::None;
        if (combo)
            unit = static_cast<viewer::TimeUnit>(combo->currentIndex());

        result.push_back({pattern, unit});
    }
    return result;
}