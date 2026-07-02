#include "AliasDialog.h"
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

AliasDialog::AliasDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("自动重命名设置"));
    setMinimumSize(500, 400);

    auto* mainLayout = new QVBoxLayout(this);

    // ---- 工具栏 ----
    auto* toolbarLayout = new QHBoxLayout();
    m_btnNew = new QPushButton(QStringLiteral("新建"), this);
    m_btnDelete = new QPushButton(QStringLiteral("删除"), this);
    toolbarLayout->addWidget(m_btnNew);
    toolbarLayout->addWidget(m_btnDelete);
    toolbarLayout->addStretch();
    mainLayout->addLayout(toolbarLayout);

    // ---- 表格 ----
    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("原始数据名"), QStringLiteral("重命名为")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_table);

    // ---- 按钮栏 ----
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_btnOk = new QPushButton(QStringLiteral("确定"), this);
    m_btnCancel = new QPushButton(QStringLiteral("取消"), this);
    buttonLayout->addWidget(m_btnOk);
    buttonLayout->addWidget(m_btnCancel);
    mainLayout->addLayout(buttonLayout);

    // ---- 连接信号 ----
    connect(m_btnNew, &QPushButton::clicked, this, &AliasDialog::onNewEntry);
    connect(m_btnDelete, &QPushButton::clicked, this, &AliasDialog::onDeleteEntry);
    connect(m_btnOk, &QPushButton::clicked, this, &AliasDialog::onAccept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void AliasDialog::addRow(const QString& from, const QString& to)
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(from));
    m_table->setItem(row, 1, new QTableWidgetItem(to));
}

void AliasDialog::setAliases(const std::unordered_map<std::string, std::string>& aliases)
{
    m_table->setRowCount(0);
    for (const auto& [from, to] : aliases)
        addRow(QString::fromStdString(from), QString::fromStdString(to));
}

std::unordered_map<std::string, std::string> AliasDialog::getAliases() const
{
    std::unordered_map<std::string, std::string> result;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto* itemFrom = m_table->item(row, 0);
        auto* itemTo   = m_table->item(row, 1);
        if (!itemFrom || !itemTo)
            continue;
        QString from = itemFrom->text().trimmed();
        QString to   = itemTo->text().trimmed();
        if (from.isEmpty() || to.isEmpty())
            continue;
        result[from.toStdString()] = to.toStdString();
    }
    return result;
}

void AliasDialog::onNewEntry()
{
    addRow(QString(), QString());
    m_table->editItem(m_table->item(m_table->rowCount() - 1, 0));
}

void AliasDialog::onDeleteEntry()
{
    int row = m_table->currentRow();
    if (row >= 0)
        m_table->removeRow(row);
}

void AliasDialog::setExistingNames(const std::vector<std::string>& names)
{
    m_existingNames.clear();
    for (const auto& n : names)
        m_existingNames.insert(n);
}

void AliasDialog::onAccept()
{
    // 校验空值
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto* itemFrom = m_table->item(row, 0);
        auto* itemTo   = m_table->item(row, 1);
        QString from = itemFrom ? itemFrom->text().trimmed() : QString();
        QString to   = itemTo   ? itemTo->text().trimmed()   : QString();

        if (from.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("校验失败"),
                                 QStringLiteral("第 %1 行\"原始数据名\"不能为空").arg(row + 1));
            return;
        }
        if (to.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("校验失败"),
                                 QStringLiteral("第 %1 行\"重命名为\"不能为空").arg(row + 1));
            return;
        }
    }

    // 构建当前别名映射的临时集合（用于冲突检测）
    std::unordered_map<std::string, std::string> curAliases;
    std::unordered_set<std::string> fromSet;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        std::string from = m_table->item(row, 0)->text().trimmed().toStdString();
        std::string to   = m_table->item(row, 1)->text().trimmed().toStdString();
        curAliases[from] = to;
        fromSet.insert(from);
    }

    // 校验1：原始数据名（from）不重复
    if (fromSet.size() != curAliases.size())
    {
        QMessageBox::warning(this, QStringLiteral("校验失败"),
                             QStringLiteral("\"原始数据名\" 有重复项"));
        return;
    }

    // 校验2：重命名目标（to）不重复
    {
        std::unordered_set<std::string> toSet;
        for (const auto& [from, to] : curAliases)
        {
            if (toSet.count(to))
            {
                QMessageBox::warning(this, QStringLiteral("校验失败"),
                                     QStringLiteral("\"重命名为\" \"%1\" 重复").arg(QString::fromStdString(to)));
                return;
            }
            toSet.insert(to);
        }
    }

    // 校验3：重命名目标（to）不与已有列名冲突
    // 如果某列的 from 不在别名表中（即它不会被重命名），则它的 to 不能与已有列名相同
    if (!m_existingNames.empty())
    {
        for (const auto& [from, to] : curAliases)
        {
            // to 值在已有列名集合中，且该列名对应的 from 不在此次别名表中
            if (m_existingNames.count(to) > 0 && fromSet.count(to) == 0)
            {
                QMessageBox::warning(this, QStringLiteral("校验失败"),
                    QStringLiteral("\"重命名为\" \"%1\" 与已有列名冲突，请使用其他名称")
                        .arg(QString::fromStdString(to)));
                return;
            }
        }
    }

    accept();
}
