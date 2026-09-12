#pragma once

#include <QAbstractListModel>
#include <QMap>

struct LyricsLine final {
    qint64 timeMs = 0;
    QString text;
};

struct LyricsDocument final {
    QList<LyricsLine> lines;
    QString untimedText;
    QMap<QString, QString> metadata;
    qint64 offsetMs = 0;
};

class LyricsLineModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role { TimeMsRole = Qt::UserRole + 1, TextRole };
    Q_ENUM(Role)

    explicit LyricsLineModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] const QList<LyricsLine>& lines() const noexcept;

    void setLines(QList<LyricsLine> lines);
    void clear();
    [[nodiscard]] QString lineAt(qint64 positionMs, qint64 offsetMs) const;
    [[nodiscard]] QString previousLine(qint64 positionMs, qint64 offsetMs) const;
    [[nodiscard]] QString nextLine(qint64 positionMs, qint64 offsetMs) const;
    [[nodiscard]] int activeIndex(qint64 positionMs, qint64 offsetMs) const;
    [[nodiscard]] static LyricsDocument parseLrc(const QByteArray& contents);

private:
    QList<LyricsLine> lines_;
};
