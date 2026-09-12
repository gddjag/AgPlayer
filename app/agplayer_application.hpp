#pragma once

#include <QApplication>
#include <QFileOpenEvent>
#include <QList>
#include <QUrl>
#include <utility>

// Finder may deliver open events while the library and QML are still starting.
class AgPlayerApplication final : public QApplication {
    Q_OBJECT

public:
    using QApplication::QApplication;

    void enableFileOpenDelivery()
    {
        fileOpenReady_ = true;
        const auto pending = std::exchange(pendingFileOpens_, {});
        for (const QUrl& url : pending) emit fileOpenRequested(url);
    }

signals:
    void fileOpenRequested(const QUrl& url);

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto* open = static_cast<QFileOpenEvent*>(event);
            const QUrl url = open->url();
            if (url.isLocalFile() && !url.toLocalFile().isEmpty()) {
                if (fileOpenReady_) emit fileOpenRequested(url);
                else pendingFileOpens_.append(url);
                event->accept();
                return true;
            }
        }
        return QApplication::event(event);
    }

private:
    bool fileOpenReady_ = false;
    QList<QUrl> pendingFileOpens_;
};
