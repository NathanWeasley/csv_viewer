#include "plugin/mapping_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>

#include <iostream>

namespace
{

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

viewer::plugin::JsonDocumentPtr makeCapaDocument()
{
    QJsonObject inertia;
    inertia.insert(QStringLiteral("Izz"), 9.5);
    QJsonObject robot;
    robot.insert(QStringLiteral("link_len"), QJsonArray{1.0, 2.5, 3.0});
    robot.insert(QStringLiteral("inertia"), QJsonArray{inertia});
    QJsonObject deviceInfo;
    deviceInfo.insert(QStringLiteral("product_type"), QStringLiteral("R7"));
    QJsonObject payload;
    payload.insert(QStringLiteral("device_info"), deviceInfo);
    payload.insert(QStringLiteral("robot"), robot);
    QJsonObject root;
    root.insert(QStringLiteral("records"), QJsonArray{payload});
    root.insert(QStringLiteral("table"), QStringLiteral("ROBOT_CAPA"));
    return viewer::plugin::JsonDocumentPtr(new QJsonDocument(root));
}

void testDefinitionLoading()
{
    const QString mappingPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../../../data/log_expand_mapping.json"));
    QList<MappingDefinition> definitions;
    QList<PluginDiagnostic> diagnostics;
    check(MappingEngine::loadDefinitions(mappingPath, definitions, diagnostics),
          "load valid mapping JSON");
    check(definitions.size() == 3, "mapping definition count");
    check(diagnostics.isEmpty(), "valid mapping has no diagnostics");
}

void testMappingResolution()
{
    const QList<MappingDefinition> definitions{
        {QStringLiteral("capa.device_info.product_type"), QStringLiteral("robot_name")},
        {QStringLiteral("capa.robot.link_len[1]"), QStringLiteral("len_1")},
        {QStringLiteral("capa.robot.inertia[0].Izz"), QStringLiteral("Izz_0")},
        {QStringLiteral("capa.robot.missing"), QStringLiteral("missing_value")}
    };
    QHash<QString, viewer::plugin::JsonDocumentPtr> documents;
    documents.insert(QStringLiteral("robot.capa"), makeCapaDocument());
    QList<PluginDiagnostic> diagnostics;
    const QList<MappedVariable> variables = MappingEngine::resolve(
        definitions, documents, diagnostics);

    check(variables.size() == 3, "three successful mapped variables");
    check(diagnostics.size() == 1, "one missing-path warning");
    check(variables[0].displayValue == QStringLiteral("R7"), "string mapping value");
    check(!variables[0].expressionEligible, "string mapping is view-only");
    check(variables[1].expressionEligible && variables[1].numericValue == 2.5,
          "array index numeric mapping");
    check(variables[2].expressionEligible && variables[2].numericValue == 9.5,
          "nested repeated-object numeric mapping");
}

void testPluginBinaryLoads()
{
#ifdef QT_DEBUG
    const QString fileName = QStringLiteral("log_expandd.dll");
#else
    const QString fileName = QStringLiteral("log_expand.dll");
#endif
    const QString pluginPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../../../lib/") + fileName);
    QPluginLoader loader(pluginPath);
    QObject* instance = loader.instance();
    if (!instance)
        std::cerr << "Plugin load error: " << loader.errorString().toStdString() << '\n';
    check(instance != nullptr, "load the built log_expand plugin binary");
    const QJsonObject pluginMetadata = loader.metaData()
        .value(QStringLiteral("MetaData")).toObject();
    check(pluginMetadata.value(QStringLiteral("id")).toString()
              == QStringLiteral("log_expand"),
          "plugin binary metadata id");
    if (instance)
        check(loader.unload(), "unload the log_expand plugin binary");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    testDefinitionLoading();
    testMappingResolution();
    testPluginBinaryLoads();
    if (failures == 0)
        std::cout << "All log_expand tests passed.\n";
    return failures == 0 ? 0 : 1;
}
