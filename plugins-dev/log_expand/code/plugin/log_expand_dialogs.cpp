#include "log_expand_dialogs.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

MappedVariablesDialog::MappedVariablesDialog(
    const QList<MappedVariable>& variables, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8(u8"log_expand - 映射变量"));
    resize(850, 430);
    auto* layout = new QVBoxLayout(this);
    auto* table = new QTableWidget(variables.size(), 5, this);
    table->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"变量名"), QString::fromUtf8(u8"当前值"),
        QString::fromUtf8(u8"类型"), QString::fromUtf8(u8"来源路径"),
        QString::fromUtf8(u8"可用于表达式")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (qsizetype row = 0; row < variables.size(); ++row)
    {
        const MappedVariable& variable = variables[row];
        table->setItem(row, 0, new QTableWidgetItem(variable.name));
        table->setItem(row, 1, new QTableWidgetItem(variable.displayValue));
        table->setItem(row, 2, new QTableWidgetItem(variable.typeName));
        table->setItem(row, 3, new QTableWidgetItem(variable.source));
        table->setItem(row, 4, new QTableWidgetItem(
            variable.expressionEligible ? QString::fromUtf8(u8"是")
                                        : QString::fromUtf8(u8"否")));
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(table);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

DiagnosticsDialog::DiagnosticsDialog(
    const QList<PluginDiagnostic>& diagnostics,
    const QList<ExpansionResult>& expansionResults,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8(u8"log_expand - 告警和状态"));
    resize(820, 460);
    auto* layout = new QVBoxLayout(this);
    auto* text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    QStringList lines;
    if (diagnostics.isEmpty())
        lines.push_back(QString::fromUtf8(u8"映射告警：无"));
    else
    {
        lines.push_back(QString::fromUtf8(u8"映射告警："));
        for (const PluginDiagnostic& diagnostic : diagnostics)
        {
            lines.push_back(QStringLiteral("[%1] %2%3%4")
                .arg(diagnostic.severity == PluginDiagnosticSeverity::Error
                         ? QStringLiteral("ERROR") : QStringLiteral("WARNING"),
                     diagnostic.category,
                     diagnostic.itemName.isEmpty()
                         ? QString{} : QStringLiteral("/") + diagnostic.itemName,
                     QStringLiteral(": ") + diagnostic.message));
        }
    }
    lines.push_back({});
    lines.push_back(QString::fromUtf8(u8"扩充数据项："));
    if (expansionResults.isEmpty())
        lines.push_back(QString::fromUtf8(u8"尚无计算结果。"));
    for (const ExpansionResult& result : expansionResults)
    {
        QString detail = result.message;
        if (!result.missingSymbols.isEmpty())
            detail += QStringLiteral(" [") + result.missingSymbols.join(QStringLiteral(", ")) + QLatin1Char(']');
        lines.push_back(QStringLiteral("[%1] %2: %3")
            .arg(result.success ? QStringLiteral("OK") : QStringLiteral("SKIPPED"),
                 result.name, detail));
    }
    text->setPlainText(lines.join(QLatin1Char('\n')));
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ExpansionEditorDialog::ExpansionEditorDialog(
    const QList<ExpansionDefinition>& definitions,
    const QStringList& viewerDataItems,
    const QList<MappedVariable>& variables,
    const QHash<QString, QString>& lastStatuses,
    const QStringList& reservedOutputNames,
    QWidget* parent)
    : QDialog(parent)
    , m_reservedOutputNames(reservedOutputNames)
{
    setWindowTitle(QString::fromUtf8(u8"log_expand - 编辑扩充数据项"));
    resize(1050, 560);
    auto* rootLayout = new QVBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* editorPanel = new QWidget(splitter);
    auto* editorLayout = new QVBoxLayout(editorPanel);
    m_table = new QTableWidget(0, 4, editorPanel);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"启用"), QString::fromUtf8(u8"数据项名称"),
        QString::fromUtf8(u8"计算表达式"), QString::fromUtf8(u8"上次状态")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    editorLayout->addWidget(m_table);

    auto* rowButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(QString::fromUtf8(u8"新增"), editorPanel);
    auto* removeButton = new QPushButton(QString::fromUtf8(u8"删除"), editorPanel);
    rowButtons->addWidget(addButton);
    rowButtons->addWidget(removeButton);
    rowButtons->addStretch();
    editorLayout->addLayout(rowButtons);
    connect(addButton, &QPushButton::clicked, this, [this]()
    {
        addRow({true, {}, {}});
    });
    connect(removeButton, &QPushButton::clicked, this, [this]()
    {
        QList<int> rows;
        for (const QModelIndex& index : m_table->selectionModel()->selectedRows())
            rows.push_back(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            m_table->removeRow(row);
    });

    auto* symbols = new QTabWidget(splitter);
    m_dataItems = new QListWidget(symbols);
    m_dataItems->addItems(viewerDataItems);
    symbols->addTab(m_dataItems, QString::fromUtf8(u8"Viewer 数据项"));
    m_mappedVariables = new QListWidget(symbols);
    for (const MappedVariable& variable : variables)
    {
        if (variable.expressionEligible)
            m_mappedVariables->addItem(variable.name);
    }
    symbols->addTab(m_mappedVariables, QString::fromUtf8(u8"映射标量"));
    connect(m_dataItems, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { if (item) insertSymbol(item->text()); });
    connect(m_mappedVariables, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { if (item) insertSymbol(item->text()); });

    splitter->addWidget(editorPanel);
    splitter->addWidget(symbols);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    rootLayout->addWidget(splitter);

    for (const ExpansionDefinition& definition : definitions)
        addRow(definition, lastStatuses.value(definition.name));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExpansionEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

void ExpansionEditorDialog::addRow(
    const ExpansionDefinition& definition, const QString& status)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* enabled = new QTableWidgetItem;
    enabled->setFlags(enabled->flags() | Qt::ItemIsUserCheckable);
    enabled->setCheckState(definition.enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(row, 0, enabled);
    m_table->setItem(row, 1, new QTableWidgetItem(definition.name));
    m_table->setItem(row, 2, new QTableWidgetItem(definition.expression));
    auto* statusItem = new QTableWidgetItem(status);
    statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 3, statusItem);
}

void ExpansionEditorDialog::insertSymbol(const QString& symbol)
{
    int row = m_table->currentRow();
    if (row < 0)
        return;
    QTableWidgetItem* expression = m_table->item(row, 2);
    if (!expression)
        return;
    QString text = expression->text();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(' ')))
        text += QLatin1Char(' ');
    expression->setText(text + symbol);
    m_table->setCurrentCell(row, 2);
}

QList<ExpansionDefinition> ExpansionEditorDialog::definitions() const
{
    QList<ExpansionDefinition> result;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        result.push_back({
            m_table->item(row, 0)->checkState() == Qt::Checked,
            m_table->item(row, 1)->text().trimmed(),
            m_table->item(row, 2)->text().trimmed()});
    }
    return result;
}

void ExpansionEditorDialog::accept()
{
    const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    const QSet<QString> reserved(m_reservedOutputNames.begin(),
                                 m_reservedOutputNames.end());
    QSet<QString> names;
    for (const ExpansionDefinition& definition : definitions())
    {
        if (!identifier.match(definition.name).hasMatch()
            || definition.expression.isEmpty())
        {
            QMessageBox::warning(this, windowTitle(),
                QString::fromUtf8(u8"数据项名称必须是有效标识符，且表达式不能为空。"));
            return;
        }
        if (names.contains(definition.name) || reserved.contains(definition.name))
        {
            QMessageBox::warning(this, windowTitle(),
                QString::fromUtf8(u8"扩充数据项名称重复，或与非本插件数据项冲突：")
                    + definition.name);
            return;
        }
        names.insert(definition.name);
    }
    QDialog::accept();
}
