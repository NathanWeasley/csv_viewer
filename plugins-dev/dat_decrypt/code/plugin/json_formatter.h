#pragma once

#include <QJsonDocument>
#include <QString>

namespace datdecrypt::json
{

// Pretty JSON used by both the text viewer and exported files. Root records
// are emitted first, metadata follows, and diagnostics/warnings are last.
// Arrays containing only JSON numbers stay on one physical line.
QString formatDocument(const QJsonDocument& document);

} // namespace datdecrypt::json
