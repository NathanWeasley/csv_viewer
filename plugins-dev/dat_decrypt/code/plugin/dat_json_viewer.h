#pragma once

#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;

class DatJsonViewer final : public QWidget
{
public:
    explicit DatJsonViewer(QWidget* parent = nullptr);

    void setDocuments(
        const QList<viewer::plugin::JsonDocumentInfo>& documents,
        const QHash<QString, viewer::plugin::JsonDocumentPtr>& contents);

private:
    void showCurrentDocument();

    QComboBox* m_documents = nullptr;
    QLabel* m_source = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPlainTextEdit* m_text = nullptr;
    QPlainTextEdit* m_diagnostics = nullptr;
    QHash<QString, viewer::plugin::JsonDocumentInfo> m_info;
    QHash<QString, viewer::plugin::JsonDocumentPtr> m_contents;
};
