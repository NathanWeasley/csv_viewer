#include "HighlightDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPixmap>
#include <QIcon>

// ============================================================
// 35 色预设（与 UI.cpp 中 colorPresets 保持一致）
// ============================================================
static const QColor kColorPresets[] = {
    QColor(0, 114, 189),     // 0:  MATLAB Blue
    QColor(217, 83, 25),     // 1:  MATLAB Orange
    QColor(237, 177, 32),    // 2:  MATLAB Yellow
    QColor(126, 47, 142),    // 3:  MATLAB Purple
    QColor(119, 172, 48),    // 4:  MATLAB Green
    QColor(77, 190, 238),    // 5:  MATLAB Cyan
    QColor(162, 20, 47),     // 6:  MATLAB Red
    QColor(0, 147, 147),     // 7:  Teal
    QColor(255, 61, 127),    // 8:  Pink
    QColor(100, 149, 237),   // 9:  Cornflower
    QColor(205, 92, 92),     // 10: Indian Red
    QColor(85, 107, 47),     // 11: Olive Drab
    QColor(186, 85, 211),    // 12: Medium Orchid
    QColor(0, 191, 255),     // 13: Deep Sky Blue
    QColor(255, 215, 0),     // 14: Gold
    QColor(50, 205, 50),     // 15: Lime Green
    QColor(255, 99, 71),     // 16: Tomato
    QColor(64, 224, 208),    // 17: Turquoise
    QColor(255, 140, 0),     // 18: Dark Orange
    QColor(138, 43, 226),    // 19: Blue Violet
    QColor(0, 206, 209),     // 20: Dark Turquoise
    QColor(255, 20, 147),    // 21: Deep Pink
    QColor(154, 205, 50),    // 22: Yellow Green
    QColor(70, 130, 180),    // 23: Steel Blue
    QColor(240, 128, 128),   // 24: Light Coral
    QColor(147, 112, 219),   // 25: Medium Purple
    QColor(60, 179, 113),    // 26: Medium Sea Green
    QColor(255, 160, 122),   // 27: Light Salmon
    QColor(0, 191, 143),     // 28: Mint
    QColor(255, 69, 0),      // 29: Orange Red
    QColor(65, 105, 225),    // 30: Royal Blue
    QColor(218, 165, 32),    // 31: Goldenrod
    QColor(Qt::black),       // 32: Black
    QColor(128, 128, 128),   // 33: Gray
    QColor(192, 192, 192)    // 34: Silver
};
static const int kColorCount = sizeof(kColorPresets) / sizeof(kColorPresets[0]);

// ============================================================
// 条件名称映射
// ============================================================
static QString conditionToChinese(viewer::HighlightCondition cond)
{
    switch (cond)
    {
    case viewer::HighlightCondition::Greater:  return QString::fromUtf8("大于");
    case viewer::HighlightCondition::Less:     return QString::fromUtf8("小于");
    case viewer::HighlightCondition::Equal:    return QString::fromUtf8("等于");
    case viewer::HighlightCondition::NotEqual: return QString::fromUtf8("不等于");
    case viewer::HighlightCondition::Between:  return QString::fromUtf8("介于");
    default: return "?";
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

HighlightDialog::HighlightDialog(const std::vector<std::string>& columnNames,
                                   QWidget* parent)
    : QDialog(parent)
    , m_columnNames(columnNames)
{
    setWindowTitle(QString::fromUtf8("高亮规则配置"));
    setFixedSize(750, 480);
    buildUI();
}

// ============================================================
// buildUI
// ============================================================

void HighlightDialog::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // ---- 中央分栏：左右分栏 ----
    auto* splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(splitter, 1);

    // =================== 左侧：规则编辑 ===================
    auto* leftGroup = new QGroupBox(QString::fromUtf8("规则编辑"));
    auto* leftLayout = new QFormLayout(leftGroup);

    // 数据列
    m_cmbColumn = new QComboBox();
    for (const auto& name : m_columnNames)
        m_cmbColumn->addItem(QString::fromStdString(name));
    leftLayout->addRow(QString::fromUtf8("数据列："), m_cmbColumn);

    // 条件
    m_cmbCondition = new QComboBox();
    m_cmbCondition->addItem(QString::fromUtf8("大于"));
    m_cmbCondition->addItem(QString::fromUtf8("小于"));
    m_cmbCondition->addItem(QString::fromUtf8("等于"));
    m_cmbCondition->addItem(QString::fromUtf8("不等于"));
    m_cmbCondition->addItem(QString::fromUtf8("介于"));
    connect(m_cmbCondition, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HighlightDialog::onConditionChanged);
    leftLayout->addRow(QString::fromUtf8("条件："), m_cmbCondition);

    // 值行：m_spnValue1 + [m_lblBetween + m_spnValue2]（介于时显示）
    m_spnValue1 = new QDoubleSpinBox();
    m_spnValue1->setRange(-1e15, 1e15);
    m_spnValue1->setDecimals(6);
    m_spnValue1->setValue(0.0);

    m_spnValue2 = new QDoubleSpinBox();
    m_spnValue2->setRange(-1e15, 1e15);
    m_spnValue2->setDecimals(6);
    m_spnValue2->setValue(0.0);
    m_spnValue2->setVisible(false);

    m_lblBetween = new QLabel("  " + QString::fromUtf8("与") + " ");
    m_lblBetween->setVisible(false);

    auto* betweenRow = new QHBoxLayout();
    betweenRow->setContentsMargins(0, 0, 0, 0);
    betweenRow->setSpacing(4);
    betweenRow->addWidget(m_spnValue1);
    betweenRow->addWidget(m_lblBetween);
    betweenRow->addWidget(m_spnValue2);
    betweenRow->addStretch();

    auto* valueContainer = new QWidget();
    valueContainer->setLayout(betweenRow);
    leftLayout->addRow(QString::fromUtf8("值："), valueContainer);

    // 颜色
    m_cmbColor = new QComboBox();
    for (int i = 0; i < kColorCount; ++i)
    {
        QPixmap swatch(20, 14);
        swatch.fill(kColorPresets[i]);
        m_cmbColor->addItem(QIcon(swatch), QString(), QVariant::fromValue(kColorPresets[i]));
    }
    m_cmbColor->setCurrentIndex(2); // 默认黄色
    leftLayout->addRow(QString::fromUtf8("颜色："), m_cmbColor);

    // 透明度
    m_spnAlpha = new QSpinBox();
    m_spnAlpha->setRange(0, 255);
    m_spnAlpha->setValue(100);
    leftLayout->addRow(QString::fromUtf8("透明度："), m_spnAlpha);

    // 文字标注
    m_txtLabel = new QLineEdit();
    m_txtLabel->setPlaceholderText(QString::fromUtf8("可选标注文字"));
    leftLayout->addRow(QString::fromUtf8("标注："), m_txtLabel);

    // 添加按钮
    m_btnAdd = new QPushButton(QString::fromUtf8("添加规则 →"));
    m_btnAdd->setFixedHeight(32);
    connect(m_btnAdd, &QPushButton::clicked, this, &HighlightDialog::onAddRule);
    leftLayout->addRow(m_btnAdd);

    splitter->addWidget(leftGroup);

    // =================== 右侧：规则列表 ===================
    auto* rightGroup = new QGroupBox(QString::fromUtf8("规则列表"));
    auto* rightLayout = new QVBoxLayout(rightGroup);

    // 按钮栏
    auto* btnBar = new QHBoxLayout();
    m_btnRemove = new QPushButton(QString::fromUtf8("删除"));
    m_btnRemove->setEnabled(false);
    connect(m_btnRemove, &QPushButton::clicked, this, &HighlightDialog::onRemoveRule);
    btnBar->addWidget(m_btnRemove);

    m_btnRead = new QPushButton(QString::fromUtf8("读取"));
    m_btnRead->setEnabled(false);
    connect(m_btnRead, &QPushButton::clicked, this, &HighlightDialog::onReadRule);
    btnBar->addWidget(m_btnRead);

    btnBar->addStretch();
    rightLayout->addLayout(btnBar);

    // 规则列表
    m_listRules = new QListWidget();
    connect(m_listRules, &QListWidget::currentRowChanged,
            this, &HighlightDialog::onRuleListSelectionChanged);
    rightLayout->addWidget(m_listRules, 1);

    splitter->addWidget(rightGroup);

    // 锁定分栏：禁止折叠，固定比例 2:3
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    // ---- 底部按钮 ----
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("确认"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

// ============================================================
// 公共接口
// ============================================================

std::vector<viewer::HighlightRule> HighlightDialog::getRules() const
{
    return m_rules;
}

void HighlightDialog::setRules(const std::vector<viewer::HighlightRule>& rules)
{
    m_rules = rules;

    // 刷新列表显示
    m_listRules->blockSignals(true);
    m_listRules->clear();
    for (const auto& rule : m_rules)
        m_listRules->addItem(ruleToString(rule));
    m_listRules->blockSignals(false);

    m_btnRemove->setEnabled(false);
    m_btnRead->setEnabled(false);
}

// ============================================================
// 槽函数
// ============================================================

void HighlightDialog::onAddRule()
{
    viewer::HighlightRule rule = buildRuleFromUI();

    // 去重检查
    for (const auto& existing : m_rules)
    {
        if (existing == rule)
            return;  // 已存在，不重复添加
    }

    m_rules.push_back(rule);

    // 更新列表
    m_listRules->blockSignals(true);
    m_listRules->addItem(ruleToString(rule));
    m_listRules->blockSignals(false);
}

void HighlightDialog::onRemoveRule()
{
    int idx = m_listRules->currentRow();
    if (idx < 0 || idx >= static_cast<int>(m_rules.size()))
        return;

    m_rules.erase(m_rules.begin() + idx);

    m_listRules->blockSignals(true);
    delete m_listRules->takeItem(idx);
    m_listRules->blockSignals(false);

    m_btnRemove->setEnabled(false);
    m_btnRead->setEnabled(false);
}

void HighlightDialog::onReadRule()
{
    int idx = m_listRules->currentRow();
    if (idx < 0 || idx >= static_cast<int>(m_rules.size()))
        return;

    const viewer::HighlightRule& rule = m_rules[idx];

    // 填入左侧控件
    fillUIToRule(rule);

    // 从右侧移除
    m_rules.erase(m_rules.begin() + idx);

    m_listRules->blockSignals(true);
    delete m_listRules->takeItem(idx);
    m_listRules->blockSignals(false);

    m_btnRemove->setEnabled(false);
    m_btnRead->setEnabled(false);
}

void HighlightDialog::onConditionChanged(int index)
{
    bool isBetween = (index == 4); // "介于"

    m_spnValue2->setVisible(isBetween);
    m_lblBetween->setVisible(isBetween);
}

void HighlightDialog::onRuleListSelectionChanged()
{
    bool hasSelection = (m_listRules->currentRow() >= 0);
    m_btnRemove->setEnabled(hasSelection);
    m_btnRead->setEnabled(hasSelection);
}

// ============================================================
// 辅助方法
// ============================================================

QString HighlightDialog::ruleToString(const viewer::HighlightRule& rule) const
{
    QString condStr = conditionToChinese(rule.condition);

    QString valueStr;
    if (rule.condition == viewer::HighlightCondition::Between)
    {
        valueStr = QString::number(rule.value1) + " " + QString::fromUtf8("与") + " " + QString::number(rule.value2);
    }
    else
    {
        valueStr = QString::number(rule.value1);
    }

    // 颜色色块名
    QString colorName = rule.color.name().toUpper();

    QString labelPart;
    if (!rule.label.empty())
        labelPart = QString::fromUtf8(" [") + QString::fromStdString(rule.label) + "]";

    return QString::fromStdString(rule.dataColumn)
        + " " + condStr + " " + valueStr
        + " | " + colorName
        + " α=" + QString::number(rule.alpha)
        + labelPart;
}

viewer::HighlightRule HighlightDialog::buildRuleFromUI() const
{
    viewer::HighlightRule rule;

    rule.dataColumn = m_cmbColumn->currentText().toStdString();

    // 条件
    int condIdx = m_cmbCondition->currentIndex();
    switch (condIdx)
    {
    case 0: rule.condition = viewer::HighlightCondition::Greater;  break;
    case 1: rule.condition = viewer::HighlightCondition::Less;     break;
    case 2: rule.condition = viewer::HighlightCondition::Equal;    break;
    case 3: rule.condition = viewer::HighlightCondition::NotEqual; break;
    case 4: rule.condition = viewer::HighlightCondition::Between;  break;
    default: rule.condition = viewer::HighlightCondition::Greater; break;
    }

    rule.value1 = m_spnValue1->value();
    rule.value2 = m_spnValue2->value();

    QColor c = m_cmbColor->currentData(Qt::UserRole).value<QColor>();
    if (c.isValid())
        rule.color = c;
    else
        rule.color = QColor(255, 255, 0);

    rule.alpha = m_spnAlpha->value();
    rule.label = m_txtLabel->text().toStdString();

    return rule;
}

void HighlightDialog::fillUIToRule(const viewer::HighlightRule& rule)
{
    // 数据列
    int colIdx = -1;
    for (int i = 0; i < m_cmbColumn->count(); ++i)
    {
        if (m_cmbColumn->itemText(i).toStdString() == rule.dataColumn)
        {
            colIdx = i;
            break;
        }
    }
    if (colIdx >= 0)
        m_cmbColumn->setCurrentIndex(colIdx);

    // 条件
    int condIdx = 0;
    switch (rule.condition)
    {
    case viewer::HighlightCondition::Greater:  condIdx = 0; break;
    case viewer::HighlightCondition::Less:     condIdx = 1; break;
    case viewer::HighlightCondition::Equal:    condIdx = 2; break;
    case viewer::HighlightCondition::NotEqual: condIdx = 3; break;
    case viewer::HighlightCondition::Between:  condIdx = 4; break;
    }
    m_cmbCondition->setCurrentIndex(condIdx);

    m_spnValue1->setValue(rule.value1);
    m_spnValue2->setValue(rule.value2);

    // 颜色
    for (int i = 0; i < m_cmbColor->count(); ++i)
    {
        QColor c = m_cmbColor->itemData(i, Qt::UserRole).value<QColor>();
        if (c == rule.color)
        {
            m_cmbColor->setCurrentIndex(i);
            break;
        }
    }

    m_spnAlpha->setValue(rule.alpha);
    m_txtLabel->setText(QString::fromStdString(rule.label));
}