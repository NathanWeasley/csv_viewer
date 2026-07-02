#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================
// AliasDialog: 列名别名管理对话框
// ============================================================
class AliasDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AliasDialog(QWidget* parent = nullptr);

    // 从外部加载已有的别名映射（from → to）
    void setAliases(const std::unordered_map<std::string, std::string>& aliases);

    // 设置已有的所有列名列表（用于防重名校验）
    void setExistingNames(const std::vector<std::string>& names);

    // 获取用户编辑后的别名映射
    std::unordered_map<std::string, std::string> getAliases() const;

private slots:
    void onNewEntry();
    void onDeleteEntry();
    void onAccept();

private:
    void addRow(const QString& from, const QString& to);

    QTableWidget* m_table = nullptr;
    QPushButton* m_btnNew = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnOk = nullptr;
    QPushButton* m_btnCancel = nullptr;

    // 所有已存在的列名（用于检查重命名目标是否与现有列名冲突）
    std::unordered_set<std::string> m_existingNames;
};
