#include "dat_json_viewer.h"
#include "json_formatter.h"

#include <QComboBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTextOption>
#include <QTreeWidget>
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

QString scalarText(const QJsonValue& value)
{
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray encoded = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (encoded.size() >= 2)
        encoded = encoded.mid(1, encoded.size() - 2);
    return QString::fromUtf8(encoded);
}

bool isNumericArray(const QJsonArray& array)
{
    for (const QJsonValue& value : array)
    {
        if (!value.isDouble())
            return false;
    }
    return true;
}

QString numericArrayText(const QJsonArray& array)
{
    QString result = QStringLiteral("[");
    for (qsizetype index = 0; index < array.size(); ++index)
    {
        if (index != 0)
            result += QStringLiteral(", ");
        result += scalarText(array[index]);
    }
    result += QLatin1Char(']');
    return result;
}

bool isMetadataKey(const QString& key)
{
    return key == QStringLiteral("table")
        || key == QStringLiteral("header")
        || key == QStringLiteral("metadata")
        || key == QStringLiteral("_metadata");
}

bool isDiagnosticKey(const QString& key)
{
    return key == QStringLiteral("diagnostics")
        || key == QStringLiteral("warnings")
        || key == QStringLiteral("errors")
        || key == QStringLiteral("_diagnostics");
}

void appendIfPresent(QStringList& result,
                     const QJsonObject& object,
                     const QString& key)
{
    if (object.contains(key) && !result.contains(key))
        result.push_back(key);
}

QStringList treeKeys(const QJsonObject& object, bool documentRoot)
{
    const QStringList source = object.keys();
    if (!documentRoot)
        return source;

    QStringList result;
    result.reserve(source.size());
    appendIfPresent(result, object, QStringLiteral("records"));
    for (const QString& key : source)
    {
        if (key != QStringLiteral("records")
            && !isMetadataKey(key) && !isDiagnosticKey(key))
        {
            result.push_back(key);
        }
    }
    appendIfPresent(result, object, QStringLiteral("table"));
    appendIfPresent(result, object, QStringLiteral("header"));
    appendIfPresent(result, object, QStringLiteral("metadata"));
    appendIfPresent(result, object, QStringLiteral("_metadata"));
    for (const QString& key : source)
    {
        if (isDiagnosticKey(key))
            appendIfPresent(result, object, key);
    }
    return result;
}

void addTreeValue(QTreeWidgetItem* item,
                  const QJsonValue& value,
                  bool documentRoot = false)
{
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        item->setText(1, object.isEmpty()
            ? QStringLiteral("{}")
            : QStringLiteral("{%1}").arg(object.size()));
        for (const QString& key : treeKeys(object, documentRoot))
        {
            auto* child = new QTreeWidgetItem(item, QStringList{key});
            addTreeValue(child, object.value(key));
        }
        return;
    }

    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        if (isNumericArray(array))
        {
            item->setText(1, numericArrayText(array));
            return;
        }
        item->setText(1, array.isEmpty()
            ? QStringLiteral("[]")
            : QStringLiteral("[%1]").arg(array.size()));
        for (qsizetype index = 0; index < array.size(); ++index)
        {
            auto* child = new QTreeWidgetItem(
                item, QStringList{QStringLiteral("[%1]").arg(index)});
            addTreeValue(child, array[index]);
        }
        return;
    }

    item->setText(1, scalarText(value));
}

void populateTree(QTreeWidget* tree, const QJsonDocument& document)
{
    tree->clear();
    if (document.isObject())
    {
        const QJsonObject object = document.object();
        for (const QString& key : treeKeys(object, true))
        {
            auto* item = new QTreeWidgetItem(tree, QStringList{key});
            addTreeValue(item, object.value(key));
            if (key == QStringLiteral("records"))
                item->setExpanded(true);
        }
    }
    else if (document.isArray())
    {
        const QJsonArray array = document.array();
        for (qsizetype index = 0; index < array.size(); ++index)
        {
            auto* item = new QTreeWidgetItem(
                tree, QStringList{QStringLiteral("[%1]").arg(index)});
            addTreeValue(item, array[index]);
        }
    }
}

} // namespace

DatJsonViewer::DatJsonViewer(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    m_documents = new QComboBox(this);
    m_source = new QLabel(this);
    m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels(
        {QString::fromUtf8(u8"字段"), QString::fromUtf8(u8"值")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_diagnostics = new QPlainTextEdit(this);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setMaximumBlockCount(5000);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(m_tree, QString::fromUtf8(u8"树状"));
    tabs->addTab(m_text, QString::fromUtf8(u8"文本"));

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(tabs);
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
        m_tree->clear();
        m_text->clear();
        m_diagnostics->clear();
        return;
    }

    const JsonDocumentInfo& info = infoIt.value();
    m_source->setText(QStringLiteral("%1  |  table: %2")
                          .arg(info.sourceEntryPath, info.sourceTableName));
    const JsonDocumentPtr document = m_contents.value(documentId);
    if (document)
    {
        populateTree(m_tree, *document);
        m_text->setPlainText(datdecrypt::json::formatDocument(*document));
    }
    else
    {
        m_tree->clear();
        m_text->setPlainText(QStringLiteral("No JSON document is available."));
    }
    m_diagnostics->setPlainText(diagnosticText(info));
}
