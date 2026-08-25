#include "metadata_text.hpp"

#include <QByteArray>
#include <QStringConverter>

namespace agplayer::qt {

QString decodeMetadataText(const char* value)
{
    if (value == nullptr) return {};
    const QByteArray bytes(value);
    QStringDecoder utf8(QStringConverter::Encoding::Utf8);
    const QString strictUtf8 = utf8.decode(bytes);
    if (!utf8.hasError()) {
        // Valid UTF-8 is authoritative; a literal "Ã©" must not become "é".
        return strictUtf8;
    }
    QStringDecoder cp936("GBK");
    const QString legacy = cp936.decode(bytes);
    const QByteArray reversible = legacy.toLatin1();
    QStringDecoder repairedDecoder(QStringConverter::Encoding::Utf8);
    const QString repaired = repairedDecoder.decode(reversible);
    if (!reversible.isEmpty() && !reversible.contains('?') && !repairedDecoder.hasError()
        && repaired.toLatin1() == reversible && repaired != legacy) {
        return repaired;
    }
    return legacy;
}

} // namespace agplayer::qt
