#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <string>
#include <unordered_map>
#include <utility>

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
};