#include "dat_json_viewer.h"

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QVBoxLayout>

using namespace viewer::plugin;

namespace
{

QString diagnosticText(const JsonDocumentInfo& info)
{
    QStringList lines;
    if (!info.error.isEmpty())
        lines.push_back(QStringLiteral("ERROR: %1").arg(info.error));
    for (const JsonDiagnostic& diagnostic : info.diagnostics)
    {
        QString severity;
        switch (diagnostic.severity)
        {
        case JsonDiagnosticSeverity::Information: severity = QStringLiteral("INFO"); break;
        case JsonDiagnosticSeverity::Warning: severity = QStringLiteral("WARNING"); break;
        case JsonDiagnosticSeverity::Error: severity = QStringLiteral("ERROR"); break;
        }
        QString location;
        if (!diagnostic.path.isEmpty())
            location += QStringLiteral(" path=%1").arg(diagnostic.path);
        if (diagnostic.recordIndex >= 0)
            location += QStringLiteral(" record=%1").arg(diagnostic.recordIndex);
        if (diagnostic.byteOffset >= 0)
            location += QStringLiteral(" offset=%1").arg(diagnostic.byteOffset);
        lines.push_back(QStringLiteral("%1 [%2]%3 %4")
                            .arg(severity, diagnostic.code, location, diagnostic.message));
    }
    return lines.isEmpty() ? QStringLiteral("No diagnostics.") : lines.join('\n');
}

} // namespace

DatJsonViewer::DatJsonViewer(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_documents = new QComboBox(this);
    m_source = new QLabel(this);
    m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_json = new QPlainTextEdit(this);
    m_json->setReadOnly(true);
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setMaximumBlockCount(5000);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_json);
    splitter->addWidget(m_diagnostics);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);

    layout->addWidget(m_documents);
    layout->addWidget(m_source);
    layout->addWidget(splitter, 1);

    connect(m_documents, &QComboBox::currentIndexChanged,
            this, [this]() { showCurrentDocument(); });
}

void DatJsonViewer::setDocuments(
    const QList<JsonDocumentInfo>& documents,
    const QHash<QString, JsonDocumentPtr>& contents)
{
    const QString selected = m_documents->currentData().toString();
    m_info.clear();
    m_contents = contents;
    m_documents->clear();

    for (const JsonDocumentInfo& info : documents)
    {
        m_info.insert(info.documentId, info);
        const QString label = info.displayName.isEmpty() ? info.documentId : info.displayName;
        m_documents->addItem(label, info.documentId);
    }

    const int previousIndex = m_documents->findData(selected);
    if (previousIndex >= 0)
        m_documents->setCurrentIndex(previousIndex);
    showCurrentDocument();
}

void DatJsonViewer::showCurrentDocument()
{
    const QString documentId = m_documents->currentData().toString();
    const auto infoIt = m_info.constFind(documentId);
    if (infoIt == m_info.constEnd())
    {
        m_source->clear();
        m_json->clear();
        m_diagnostics->clear();
        return;
    }

    const JsonDocumentInfo& info = infoIt.value();
    m_source->setText(QStringLiteral("%1  |  table: %2")
                          .arg(info.sourceEntryPath, info.sourceTableName));
    const JsonDocumentPtr document = m_contents.value(documentId);
    m_json->setPlainText(document
        ? QString::fromUtf8(document->toJson(QJsonDocument::Indented))
        : QStringLiteral("No JSON document is available."));
    m_diagnostics->setPlainText(diagnosticText(info));
}
