#include "code_viewer/stylemgr/theme_icon.h"

#include <QFile>
#include <QGuiApplication>
#include <QList>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QSvgRenderer>

#include <algorithm>

namespace viewer::theme
{
namespace
{

constexpr const char* kColorStroke = "#000000";
constexpr const char* kColorFill = "#FFFFFF";

QString expandHex(const QString& hex)
{
    if (hex.length() == 7)
        return hex;
    if (hex.length() == 4 && hex[0] == QLatin1Char('#'))
    {
        return QStringLiteral("#%1%1%2%2%3%3")
            .arg(hex[1]).arg(hex[2]).arg(hex[3]);
    }
    return hex;
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

} // namespace

QString normalizeSvgColors(const QString& svgText, bool darkMode)
{
    const QString strokeColor = darkMode
        ? QStringLiteral("#D0D0D0") : QStringLiteral("#1A1A1A");
    const QString fillColor = darkMode
        ? QStringLiteral("#2D2D2D") : QStringLiteral("#F0F0F0");
    const QString expandedStroke = expandHex(QLatin1String(kColorStroke));
    const QString expandedFill = expandHex(QLatin1String(kColorFill));

    QString result = svgText;
    const QRegularExpression regex(
        R"((stroke|fill)[\s]*[=:][\s]*\"?(#[0-9a-fA-F]{3,6})\"?[\s;]?)");
    auto iterator = regex.globalMatch(result);

    struct Match
    {
        qsizetype position = 0;
        qsizetype length = 0;
        QString color;
    };
    QList<Match> matches;
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        matches.push_back({match.capturedStart(2),
                           match.capturedLength(2),
                           match.captured(2)});
    }

    std::sort(matches.begin(), matches.end(), [](const Match& left, const Match& right)
    {
        return left.position > right.position;
    });

    for (const Match& match : matches)
    {
        const QString expanded = expandHex(match.color);
        QString replacement;
        if (expanded.compare(expandedStroke, Qt::CaseInsensitive) == 0)
            replacement = strokeColor;
        else if (expanded.compare(expandedFill, Qt::CaseInsensitive) == 0)
            replacement = fillColor;
        else
            continue;
        result.replace(match.position, match.length, replacement);
    }

    // SVG 未声明 fill 时默认使用黑色。将根节点的默认填充也设为主题前景色，
    // 使依赖默认黑色的路径与显式 fill="#000000" 保持一致。
    const QRegularExpression svgTagRegex(
        R"(<svg\b([^>]*)>)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression fillAttributeRegex(
        R"(\bfill\s*=)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch svgTagMatch = svgTagRegex.match(result);
    if (svgTagMatch.hasMatch()
        && !fillAttributeRegex.match(svgTagMatch.captured()).hasMatch())
    {
        result.insert(svgTagMatch.capturedStart() + 4,
                      QStringLiteral(" fill=\"%1\"").arg(strokeColor));
    }
    return result;
}

QIcon createSvgIcon(const QString& svgText,
                    bool darkMode,
                    int logicalSize,
                    qreal devicePixelRatio,
                    QString* errorMessage)
{
    setError(errorMessage, {});
    if (svgText.trimmed().isEmpty())
    {
        setError(errorMessage, QStringLiteral("SVG 内容为空"));
        return {};
    }

    const QByteArray svgBytes = normalizeSvgColors(svgText, darkMode).toUtf8();
    QSvgRenderer renderer(svgBytes);
    if (!renderer.isValid())
    {
        setError(errorMessage, QStringLiteral("SVG 格式无效或 Qt 无法解析"));
        return {};
    }

    const int safeLogicalSize = qMax(1, logicalSize);
    qreal dpr = devicePixelRatio;
    if (dpr <= 0.0)
    {
        const QScreen* screen = QGuiApplication::primaryScreen();
        dpr = screen ? screen->devicePixelRatio() : 1.0;
    }
    dpr = qMax<qreal>(1.0, dpr);

    const int physicalSize = qMax(1, qRound(safeLogicalSize * dpr));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return QIcon(pixmap);
}

QIcon loadSvgIcon(const QString& sourcePath,
                  bool darkMode,
                  int logicalSize,
                  qreal devicePixelRatio,
                  QString* errorMessage)
{
    setError(errorMessage, {});
    const QString path = sourcePath.trimmed();
    if (path.isEmpty())
    {
        setError(errorMessage, QStringLiteral("SVG 资源路径为空"));
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage,
                 QStringLiteral("无法打开 SVG 资源：%1").arg(file.errorString()));
        return {};
    }
    return createSvgIcon(QString::fromUtf8(file.readAll()), darkMode,
                         logicalSize, devicePixelRatio, errorMessage);
}

} // namespace viewer::theme
