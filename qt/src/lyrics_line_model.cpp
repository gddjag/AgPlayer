#include "lyrics_line_model.hpp"
#include "metadata_text.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace {

const QRegularExpression kTimestamp(
    QStringLiteral(R"(\[(\d+):(\d{1,2})(?:[\.:](\d{1,3}))?\])"));
const QRegularExpression kMetadata(
    QStringLiteral(R"(^\[([[:alnum:]_ -]+):(.*)\]$)"));

qint64 fractionToMilliseconds(const QString& fraction)
{
    if (fraction.isEmpty()) return 0;
    if (fraction.size() == 1) return fraction.toLongLong() * 100;
    if (fraction.size() == 2) return fraction.toLongLong() * 10;
    return fraction.left(3).toLongLong();
}

} // namespace

LyricsLineModel::LyricsLineModel(QObject* parent) : QAbstractListModel(parent) {}

int LyricsLineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : lines_.size();
}

QVariant LyricsLineModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= lines_.size()) return {};
    const LyricsLine& line = lines_.at(index.row());
    if (role == TimeMsRole) return line.timeMs;
    if (role == TextRole) return line.text;
    return {};
}

QHash<int, QByteArray> LyricsLineModel::roleNames() const
{
    return {{TimeMsRole, "timeMs"}, {TextRole, "text"}};
}

const QList<LyricsLine>& LyricsLineModel::lines() const noexcept { return lines_; }

void LyricsLineModel::setLines(QList<LyricsLine> lines)
{
    std::stable_sort(lines.begin(), lines.end(), [](const LyricsLine& left,
                                                    const LyricsLine& right) {
        return left.timeMs < right.timeMs;
    });
    beginResetModel();
    lines_ = std::move(lines);
    endResetModel();
}

void LyricsLineModel::clear() { setLines({}); }

// Public so LyricsService and QML can share the exact same active-line lookup.
int LyricsLineModel::activeIndex(const qint64 positionMs, const qint64 offsetMs) const
{
    const qint64 effectivePosition = positionMs - offsetMs;
    int result = -1;
    for (int index = 0; index < lines_.size(); ++index) {
        if (lines_.at(index).timeMs > effectivePosition) break;
        result = index;
    }
    return result;
}

QString LyricsLineModel::lineAt(const qint64 positionMs, const qint64 offsetMs) const
{
    const int index = activeIndex(positionMs, offsetMs);
    return index >= 0 ? lines_.at(index).text : QString();
}

QString LyricsLineModel::previousLine(const qint64 positionMs, const qint64 offsetMs) const
{
    const int index = activeIndex(positionMs, offsetMs);
    return index > 0 ? lines_.at(index - 1).text : QString();
}

QString LyricsLineModel::nextLine(const qint64 positionMs, const qint64 offsetMs) const
{
    const int index = activeIndex(positionMs, offsetMs);
    const int next = index + 1;
    return next >= 0 && next < lines_.size() ? lines_.at(next).text : QString();
}

LyricsDocument LyricsLineModel::parseLrc(const QByteArray& contents)
{
    LyricsDocument document;
    QString text = agplayer::qt::decodeMetadataText(contents.constData());
    if (!text.isEmpty() && text.front() == QChar::ByteOrderMark) text.remove(0, 1);

    QStringList untimed;
    for (const QString& rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.endsWith(QLatin1Char('\r'))
            ? rawLine.left(rawLine.size() - 1) : rawLine;
        if (line.trimmed().isEmpty()) continue;

        QRegularExpressionMatchIterator timestamps = kTimestamp.globalMatch(line);
        QList<qint64> times;
        int payloadStart = 0;
        while (timestamps.hasNext()) {
            const QRegularExpressionMatch match = timestamps.next();
            const qint64 minutes = match.captured(1).toLongLong();
            const qint64 seconds = match.captured(2).toLongLong();
            if (seconds >= 60) continue;
            times.append((minutes * 60 + seconds) * 1000
                         + fractionToMilliseconds(match.captured(3)));
            payloadStart = std::max(payloadStart, static_cast<int>(match.capturedEnd()));
        }
        if (!times.isEmpty()) {
            const QString payload = line.mid(payloadStart).trimmed();
            for (const qint64 time : times) document.lines.append({time, payload});
            continue;
        }

        const QRegularExpressionMatch metadata = kMetadata.match(line);
        if (metadata.hasMatch()) {
            const QString key = metadata.captured(1).trimmed().toLower();
            const QString value = metadata.captured(2).trimmed();
            if (key == QStringLiteral("offset")) {
                bool ok = false;
                const qint64 offset = value.toLongLong(&ok);
                if (ok) document.offsetMs = offset;
            } else {
                document.metadata.insert(key, value);
            }
            continue;
        }
        untimed.append(line);
    }
    std::stable_sort(document.lines.begin(), document.lines.end(),
                     [](const LyricsLine& left, const LyricsLine& right) {
        return left.timeMs < right.timeMs;
    });
    document.untimedText = untimed.join(QLatin1Char('\n'));
    return document;
}
