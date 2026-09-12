#include "metadata_text.hpp"

#include <QByteArray>
#include <QStringConverter>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agplayer::qt {

namespace {

QString decodeCp936(const QByteArray& bytes)
{
#ifdef Q_OS_WIN
    const int length = MultiByteToWideChar(936, 0, bytes.constData(),
                                           bytes.size(), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(936, 0, bytes.constData(), bytes.size(),
                            wide.data(), length) != length) {
        return {};
    }
    return QString::fromWCharArray(wide.data(), length);
#else
    // Qt builds with GB18030 support cover CP936 as a compatible subset. Some
    // minimal Qt packages omit it, in which case preserve the bytes visibly.
    QStringDecoder gb18030("GB18030");
    const QString decoded = gb18030.decode(bytes);
    return gb18030.hasError() ? QString::fromLatin1(bytes) : decoded;
#endif
}

} // namespace

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
    const QString legacy = decodeCp936(bytes);
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
