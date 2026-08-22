#include "view3d_config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace view3d
{
namespace
{

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool readRequiredString(
    const QJsonObject& object,
    const QString& key,
    const QString& context,
    QString* value,
    QString* error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isString() || jsonValue.toString().trimmed().isEmpty())
        return fail(error, QStringLiteral("%1.%2 must be a non-empty string.")
                               .arg(context, key));
    *value = jsonValue.toString().trimmed();
    return true;
}

bool readVector6(
    const QJsonValue& value,
    const QString& context,
    QVector3D* translation,
    QVector3D* euler,
    QString* error)
{
    if (!value.isArray() || value.toArray().size() != 6)
        return fail(error, QStringLiteral("%1 must contain six numbers.").arg(context));

    const QJsonArray values = value.toArray();
    for (const QJsonValue& item : values)
    {
        if (!item.isDouble())
            return fail(error, QStringLiteral("%1 must contain only numbers.").arg(context));
    }
    *translation = QVector3D(
        static_cast<float>(values.at(0).toDouble()),
        static_cast<float>(values.at(1).toDouble()),
        static_cast<float>(values.at(2).toDouble()));
    *euler = QVector3D(
        static_cast<float>(values.at(3).toDouble()),
        static_cast<float>(values.at(4).toDouble()),
        static_cast<float>(values.at(5).toDouble()));
    return true;
}

bool parseJointTracks(
    const QJsonValue& value,
    QMap<QString, QVector<JointVariableConfig>>* tracks,
    QString* error)
{
    if (!value.isObject())
        return fail(error, QStringLiteral("joint must be an object."));

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!it.value().isArray() || it.value().toArray().isEmpty())
            return fail(error, QStringLiteral("joint.%1 must be a non-empty array.").arg(it.key()));

        QVector<JointVariableConfig> variables;
        QSet<QString> names;
        const QJsonArray array = it.value().toArray();
        for (qsizetype index = 0; index < array.size(); ++index)
        {
            const QString context = QStringLiteral("joint.%1[%2]").arg(it.key()).arg(index);
            if (!array.at(index).isObject())
                return fail(error, QStringLiteral("%1 must be an object.").arg(context));

            const QJsonObject variableObject = array.at(index).toObject();
            JointVariableConfig variable;
            if (!readRequiredString(
                    variableObject, QStringLiteral("name"), context, &variable.name, error))
                return false;
            if (names.contains(variable.name))
                return fail(error, QStringLiteral("%1 contains duplicate variable '%2'.")
                                       .arg(context, variable.name));

            QString type;
            if (!readRequiredString(
                    variableObject, QStringLiteral("type"), context, &type, error))
                return false;
            if (type == QStringLiteral("revolute"))
                variable.type = JointType::Revolute;
            else if (type == QStringLiteral("prismatic"))
                variable.type = JointType::Prismatic;
            else
                return fail(error,
                    QStringLiteral("%1.type must be 'revolute' or 'prismatic'.")
                        .arg(context));

            names.insert(variable.name);
            variables.push_back(variable);
        }
        tracks->insert(it.key(), variables);
    }
    return true;
}

bool parseTcpTracks(
    const QJsonValue& value,
    QMap<QString, QStringList>* tracks,
    QString* error)
{
    if (!value.isObject())
        return fail(error, QStringLiteral("tcp must be an object."));

    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!it.value().isArray() || it.value().toArray().size() != 6)
            return fail(error,
                QStringLiteral("tcp.%1 must contain [x, y, z, rx, ry, rz].").arg(it.key()));

        QStringList variables;
        for (const QJsonValue& item : it.value().toArray())
        {
            if (!item.isString() || item.toString().trimmed().isEmpty())
                return fail(error,
                    QStringLiteral("tcp.%1 must contain six non-empty strings.").arg(it.key()));
            variables.push_back(item.toString().trimmed());
        }
        tracks->insert(it.key(), variables);
    }
    return true;
}

bool parseModel(const QJsonValue& value, ModelConfig* model, QString* error)
{
    if (value.isUndefined())
        return true;
    if (!value.isObject())
        return fail(error, QStringLiteral("model must be an object."));

    const QJsonObject object = value.toObject();
    model->directory = object.value(QStringLiteral("directory")).toString().trimmed();
    model->baseStl = object.value(QStringLiteral("base")).toString().trimmed();
    const QJsonValue linksValue = object.value(QStringLiteral("links"));
    if (linksValue.isUndefined())
        return true;
    if (!linksValue.isArray())
        return fail(error, QStringLiteral("model.links must be an array."));

    const QJsonArray links = linksValue.toArray();
    for (qsizetype index = 0; index < links.size(); ++index)
    {
        const QString context = QStringLiteral("model.links[%1]").arg(index);
        if (!links.at(index).isObject())
            return fail(error, QStringLiteral("%1 must be an object.").arg(context));
        const QJsonObject linkObject = links.at(index).toObject();
        ModelLinkConfig link;
        if (!readRequiredString(
                linkObject, QStringLiteral("stl"), context, &link.stlFile, error))
            return false;
        const QJsonValue origin = linkObject.value(QStringLiteral("origin"));
        if (!origin.isUndefined()
            && !readVector6(origin, context + QStringLiteral(".origin"),
                &link.translation, &link.eulerZyx, error))
            return false;
        model->links.push_back(link);
    }
    return true;
}

} // namespace

bool ConfigLoader::loadFile(
    const QString& filePath, View3DConfig* config, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString()));
    return parse(file.readAll(), config, error);
}

bool ConfigLoader::parse(
    const QByteArray& json, View3DConfig* config, QString* error)
{
    if (!config)
        return fail(error, QStringLiteral("The output config pointer is null."));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));

    View3DConfig parsed;
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toInt() != 1)
        return fail(error, QStringLiteral("version must be 1."));
    parsed.version = 1;

    const QJsonValue unitsValue = root.value(QStringLiteral("units"));
    if (unitsValue.isObject())
    {
        const QJsonObject units = unitsValue.toObject();
        parsed.positionUnit = units.value(QStringLiteral("position"))
                                  .toString(parsed.positionUnit).trimmed();
        parsed.angleUnit = units.value(QStringLiteral("angle"))
                               .toString(parsed.angleUnit).trimmed();
    }
    if (parsed.positionUnit != QStringLiteral("mm")
        && parsed.positionUnit != QStringLiteral("m"))
        return fail(error, QStringLiteral("units.position must be 'mm' or 'm'."));
    if (parsed.angleUnit != QStringLiteral("deg")
        && parsed.angleUnit != QStringLiteral("rad"))
        return fail(error, QStringLiteral("units.angle must be 'deg' or 'rad'."));
    if (!parseJointTracks(root.value(QStringLiteral("joint")), &parsed.jointTracks, error)
        || !parseTcpTracks(root.value(QStringLiteral("tcp")), &parsed.tcpTracks, error)
        || !parseModel(root.value(QStringLiteral("model")), &parsed.model, error))
        return false;

    *config = std::move(parsed);
    if (error)
        error->clear();
    return true;
}

QString jointTypeName(JointType type)
{
    return type == JointType::Revolute
        ? QStringLiteral("revolute") : QStringLiteral("prismatic");
}

} // namespace view3d
