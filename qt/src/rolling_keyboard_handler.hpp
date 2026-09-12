#pragma once

#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>

class QQuickWindow;

// Window-local routing: never registers an operating-system/global hotkey.
class RollingKeyboardHandler : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts WRITE setShortcuts NOTIFY shortcutsChanged)
public:
    explicit RollingKeyboardHandler(QQuickItem* parent = nullptr);
    ~RollingKeyboardHandler() override;
    QVariantMap shortcuts() const { return shortcuts_; }
    void setShortcuts(const QVariantMap& value);
signals:
    void shortcutsChanged();
    void actionPressed(const QString& action);
    void actionReleased(const QString& action);
    void cancelled();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void attachWindow(QQuickWindow* window);
    void cancelHeld();
    bool acceptsKeyboard() const;
    QVariantMap shortcuts_;
    QPointer<QQuickWindow> host_;
    QHash<int, QString> held_;
};
