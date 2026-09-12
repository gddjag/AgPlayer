#include "rolling_keyboard_handler.hpp"
#include <QKeyEvent>
#include <QKeySequence>
#include <QQuickWindow>

namespace {

bool hasVisiblePopupItem(const QQuickItem* parent)
{
    if (parent == nullptr) return false;
    for (QQuickItem* child : parent->childItems()) {
        if (!child->isVisible()) continue;
        if (child->inherits("QQuickPopupItem") || hasVisiblePopupItem(child)) {
            return true;
        }
    }
    return false;
}

} // namespace

RollingKeyboardHandler::RollingKeyboardHandler(QQuickItem* parent) : QQuickItem(parent)
{
    connect(this, &QQuickItem::windowChanged, this, &RollingKeyboardHandler::attachWindow);
    connect(this, &QQuickItem::enabledChanged, this, [this] { if (!isEnabled()) cancelHeld(); });
    connect(this, &QQuickItem::visibleChanged, this, [this] { if (!isVisible()) cancelHeld(); });
    attachWindow(window());
}

RollingKeyboardHandler::~RollingKeyboardHandler()
{
    disconnect(this, nullptr, this, nullptr);
    if (host_) {
        disconnect(host_, nullptr, this, nullptr);
        host_->removeEventFilter(this);
    }
    cancelHeld();
}

void RollingKeyboardHandler::setShortcuts(const QVariantMap& value)
{
    if (shortcuts_ == value) return;
    cancelHeld();
    shortcuts_ = value;
    emit shortcutsChanged();
}

void RollingKeyboardHandler::attachWindow(QQuickWindow* window)
{
    if (host_ == window) return;
    cancelHeld();
    if (host_) {
        host_->removeEventFilter(this);
        disconnect(host_, nullptr, this, nullptr);
    }
    host_ = window;
    if (!host_) return;
    host_->installEventFilter(this);
    connect(host_, &QQuickWindow::activeFocusItemChanged, this, [this] {
        if (!acceptsKeyboard()) cancelHeld();
    });
}

bool RollingKeyboardHandler::acceptsKeyboard() const
{
    if (!host_ || !host_->isActive() || !isEnabled() || !isVisible()) return false;
    if (hasVisiblePopupItem(host_->contentItem())) return false;
    for (auto* item = host_->activeFocusItem(); item; item = item->parentItem()) {
        if (item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod)) return false;
        // Modal/pop-up controls own their keyboard navigation, including arrows.
        if (item->inherits("QQuickPopupItem")) return false;
    }
    return true;
}

void RollingKeyboardHandler::cancelHeld()
{
    if (held_.isEmpty()) return;
    held_.clear();
    emit cancelled();
}

bool RollingKeyboardHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != host_) return false;
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide
            || event->type() == QEvent::FocusOut) {
        cancelHeld();
        return false;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease
            && event->type() != QEvent::ShortcutOverride) return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (event->type() == QEvent::KeyRelease) {
        if (!held_.contains(key->key())) return false;
        if (!key->isAutoRepeat()) emit actionReleased(held_.take(key->key()));
        return true;
    }
    if (!acceptsKeyboard()) return false;
    const QString sequence = QKeySequence(key->keyCombination()).toString(QKeySequence::PortableText);
    QString action;
    for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it) {
        if (!it.value().toString().isEmpty() && it.value().toString() == sequence) {
            action = it.key();
            break;
        }
    }
    if (action.isEmpty()) return false;
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (!key->isAutoRepeat() && !held_.contains(key->key())) {
        held_.insert(key->key(), action);
        emit actionPressed(action);
    }
    return true;
}
