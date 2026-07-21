#pragma once

#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <vector>
#include <string>

#include "code_viewer/plotmgr/highlight/highlight_manager.h"

// ============================================================
// HighlightDialog: 高亮规则配置对话框
//
// 布局：左右分栏
//   左侧：规则编辑区（数据列、条件、阈值、颜色、透明度、标注、添加按钮）
//   右侧：规则列表区（规则列表、删除按钮、读取按钮）
//   底部：确认 / 取消按钮
//
// 交互：
//   - 点击"添加规则"：将左侧编辑内容转为规则加入右侧列表
//   - 选中右侧规则 → 读取按钮激活，点击后回到左侧编辑区
//   - 选中右侧规则 → 删除按钮激活，点击后移除
//   - 确认：返回所有规则
// ============================================================
class HighlightDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HighlightDialog(const std::vector<std::string>& columnNames,
                              QWidget* parent = nullptr);
    ~HighlightDialog() override;

    // 获取最终确认的规则列表
    std::vector<viewer::HighlightRule> getRules() const;

    // 设置初始规则列表（用于编辑已有规则）
    void setRules(const std::vector<viewer::HighlightRule>& rules);

private slots:
    void onAddRule();
    void onRemoveRule();
    void onClearRules();
    void onCopyRules();
    void onPasteRules();
    void onReadRule();
    void onConditionChanged(int index);
    void onRuleListSelectionChanged();

private:
    void buildUI();

    // 将规则转为右侧列表显示文本
    QString ruleToString(const viewer::HighlightRule& rule) const;

    // 从左侧控件构造一条规则
    viewer::HighlightRule buildRuleFromUI() const;

    // 将规则填入左侧控件
    void fillUIToRule(const viewer::HighlightRule& rule);

    std::vector<int> selectedRuleRows() const;
    void clearRuleSelection();
    void updateRuleButtons();
    bool hasColumn(const std::string& columnName) const;

    // ---- 左侧编辑控件 ----
    QComboBox*       m_cmbColumn = nullptr;
    QComboBox*       m_cmbCondition = nullptr;
    QDoubleSpinBox*  m_spnValue1 = nullptr;
    QDoubleSpinBox*  m_spnValue2 = nullptr;
    QLabel*          m_lblBetween = nullptr;    // " 与 " 标签
    QComboBox*       m_cmbColor = nullptr;
    QSpinBox*        m_spnAlpha = nullptr;
    QLineEdit*       m_txtLabel = nullptr;
    QPushButton*     m_btnAdd = nullptr;

    // ---- 右侧列表控件 ----
    QListWidget*     m_listRules = nullptr;
    QPushButton*     m_btnRemove = nullptr;
    QPushButton*     m_btnClear = nullptr;
    QPushButton*     m_btnCopy = nullptr;
    QPushButton*     m_btnPaste = nullptr;
    QPushButton*     m_btnRead = nullptr;

    // ---- 数据 ----
    std::vector<std::string> m_columnNames;
    std::vector<viewer::HighlightRule> m_rules;  // 右侧列表对应的规则
};
