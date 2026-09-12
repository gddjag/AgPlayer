#pragma once

#include <QAbstractListModel>
#include <QStringList>

#include <vector>

struct EditorAction final {
    QString id;
    QString text;
    QString tooltip;
    QString shortcut;
    QString icon;
    bool enabled{};
    bool checked{};
    bool busy{};
};

class EditorActionModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TextRole,
        TooltipRole,
        ShortcutRole,
        IconRole,
        EnabledRole,
        CheckedRole,
        BusyRole
    };
    Q_ENUM(Role)

    explicit EditorActionModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] EditorAction* action(const QString& id) noexcept;
    [[nodiscard]] const EditorAction* action(const QString& id) const noexcept;
    [[nodiscard]] QStringList ids() const;
    void setEnabled(const QString& id, bool enabled);

private:
    std::vector<EditorAction> actions_;
};
