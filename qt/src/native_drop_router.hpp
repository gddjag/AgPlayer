#pragma once

#include <QAbstractNativeEventFilter>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QStringList>

#include <functional>

class QWindow;

class NativeDropRouter final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    enum class Target {
        Main,
        List,
        ResourceFolder,
        AudioTools
    };
    Q_ENUM(Target)

    explicit NativeDropRouter(QObject* parent = nullptr);
    ~NativeDropRouter() override;

    void registerWindow(QWindow* window, Target target);
    void registerHitTarget(QWindow* window, Target target,
                           std::function<bool(const QPointF&)> hitTest);
    void unregisterWindow(QWindow* window);
    void routeLocalPaths(Target target, const QStringList& paths);

    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    void pathsDropped(NativeDropRouter::Target target, const QStringList& paths);

private:
    struct HitTarget {
        Target target = Target::List;
        std::function<bool(const QPointF&)> hitTest;
    };
    QHash<quintptr, Target> targets_;
    QHash<quintptr, HitTarget> hitTargets_;
};

Q_DECLARE_METATYPE(NativeDropRouter::Target)
