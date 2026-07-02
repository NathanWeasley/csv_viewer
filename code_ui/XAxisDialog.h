#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>
#include <string>
#include "code_viewer/datamgr/data_manager.h"

// ============================================================
// XAxisDialog: 默认X轴规则配置对话框
//
// 两栏表格：左侧可编辑文本（匹配模式），右侧下拉框（单位）
// 底部：添加 / 删除 / 确认 / 取消
// ============================================================
class XAxisDialog : public QDialog
{
    Q_OBJECT

public:
    explicit XAxisDialog(const std::vector<viewer::XAxisRule>& rules,
                         QWidget* parent = nullptr);

    // 获取用户编辑后的规则列表
    std::vector<viewer::XAxisRule> rules() const;

private Q_SLOTS:
    void onAddRule();
    void onDeleteRule();

private:
    void addRow(const std::string& pattern, viewer::TimeUnit unit);

    QTableWidget* m_table = nullptr;
    QPushButton* m_btnAdd = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnOK = nullptr;
    QPushButton* m_btnCancel = nullptr;
};