#include "native_drop_router.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>
#include <QWindow>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

NativeDropRouter::NativeDropRouter(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<NativeDropRouter::Target>();
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

NativeDropRouter::~NativeDropRouter()
{
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

void NativeDropRouter::registerWindow(QWindow* window, Target target)
{
    if (window == nullptr) {
        return;
    }
    const quintptr handle = static_cast<quintptr>(window->winId());
    if (targets_.contains(handle) && targets_.value(handle) == target) return;
    targets_.insert(handle, target);
    window->installEventFilter(this);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(handle);
    DragAcceptFiles(hwnd, TRUE);
    CHANGEFILTERSTRUCT filter{};
    filter.cbSize = sizeof(filter);
    ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, &filter);
    constexpr UINT kWmCopyGlobalData = 0x0049U;
    ChangeWindowMessageFilterEx(hwnd, kWmCopyGlobalData, MSGFLT_ALLOW, &filter);
#endif
    connect(window, &QObject::destroyed, this, [this, handle] {
        targets_.remove(handle);
        hitTargets_.remove(handle);
    });
}

void NativeDropRouter::registerHitTarget(
    QWindow* window, const Target target,
    std::function<bool(const QPointF&)> hitTest)
{
    if (window == nullptr || !hitTest) return;
    hitTargets_.insert(static_cast<quintptr>(window->winId()),
                       HitTarget{target, std::move(hitTest)});
}

void NativeDropRouter::unregisterWindow(QWindow* window)
{
    if (window == nullptr) {
        return;
    }
    const quintptr handle = static_cast<quintptr>(window->winId());
    targets_.remove(handle);
    hitTargets_.remove(handle);
    window->removeEventFilter(this);
#ifdef Q_OS_WIN
    DragAcceptFiles(reinterpret_cast<HWND>(handle), FALSE);
#endif
}

bool NativeDropRouter::eventFilter(QObject* watched, QEvent* event)
{
    QWindow* window = qobject_cast<QWindow*>(watched);
    if (window == nullptr || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }
    const auto target = targets_.constFind(static_cast<quintptr>(window->winId()));
    if (target == targets_.cend()) {
        return QObject::eventFilter(watched, event);
    }

    const auto containsLocalDirectory = [](const QMimeData* mime) {
        if (mime == nullptr || !mime->hasUrls()) return false;
        const QList<QUrl> urls = mime->urls();
        return std::any_of(
            urls.cbegin(), urls.cend(), [](const QUrl& url) {
                return url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir();
            });
    };
    const auto resolvedTargetAt = [this, window, target](
                                      const QPointF& position) {
        Target resolvedTarget = *target;
        const auto hitTarget = hitTargets_.constFind(
            static_cast<quintptr>(window->winId()));
        if (hitTarget != hitTargets_.cend()
            && hitTarget->hitTest(position)) {
            resolvedTarget = hitTarget->target;
        }
        return resolvedTarget;
    };

    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
        // Keep the accepted action alive while Cocoa moves over a Quick window.
        // QML never received DragEnter when this router consumed it above.
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (drag->mimeData() != nullptr && drag->mimeData()->hasUrls()) {
            if (*target == Target::List
                && containsLocalDirectory(drag->mimeData())) {
                return QObject::eventFilter(watched, event);
            }
            drag->acceptProposedAction();
            return true;
        }
    }
    if (event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (drop->mimeData() == nullptr || !drop->mimeData()->hasUrls()) {
            return true;
        }
        const Target resolvedTarget = resolvedTargetAt(drop->position());
        if (*target == Target::List
            && containsLocalDirectory(drop->mimeData())) {
            return QObject::eventFilter(watched, event);
        }
        QStringList paths;
        const QList<QUrl> urls = drop->mimeData()->urls();
        paths.reserve(urls.size());
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }
        if (!paths.isEmpty()) {
            routeLocalPaths(resolvedTarget, paths);
            drop->acceptProposedAction();
        }
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void NativeDropRouter::routeLocalPaths(Target target, const QStringList& paths)
{
    QStringList normalized;
    QSet<QString> seen;
    normalized.reserve(paths.size());
    for (const QString& path : paths) {
        const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(path));
#ifdef Q_OS_WIN
        const QString key = clean.toCaseFolded();
#else
        const QString key = clean;
#endif
        if (!clean.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            normalized.append(clean);
        }
    }
    if (!normalized.isEmpty()) {
        emit pathsDropped(target, normalized);
    }
}

bool NativeDropRouter::nativeEventFilter(const QByteArray& eventType,
                                         void* message, qintptr* result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    const auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage == nullptr || nativeMessage->message != WM_DROPFILES) {
        return false;
    }
    const quintptr handle = reinterpret_cast<quintptr>(nativeMessage->hwnd);
    const auto target = targets_.constFind(handle);
    if (target == targets_.cend()) {
        return false;
    }

    const HDROP drop = reinterpret_cast<HDROP>(nativeMessage->wParam);
    Target resolvedTarget = *target;
    POINT clientPoint{};
    if (DragQueryPoint(drop, &clientPoint)) {
        const auto hitTarget = hitTargets_.constFind(handle);
        if (hitTarget != hitTargets_.cend()
            && hitTarget->hitTest(QPointF(clientPoint.x, clientPoint.y))) {
            resolvedTarget = hitTarget->target;
        }
    }
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
    QStringList paths;
    paths.reserve(static_cast<qsizetype>(count));
    for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring buffer(static_cast<std::size_t>(length) + 1U, L'\0');
        DragQueryFileW(drop, index, buffer.data(), length + 1U);
        paths.append(QString::fromWCharArray(buffer.c_str(),
                                             static_cast<qsizetype>(length)));
    }
    DragFinish(drop);
    routeLocalPaths(resolvedTarget, paths);
    return true;
#else
    Q_UNUSED(message)
    return false;
#endif
}
