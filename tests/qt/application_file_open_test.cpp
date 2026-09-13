#include "agplayer_application.hpp"

#include <QSignalSpy>
#include <QTest>

class ApplicationFileOpenTest : public QObject {
    Q_OBJECT
private slots:
    void startupAndRunningDelivery()
    {
        auto* app = static_cast<AgPlayerApplication*>(qApp);
        QSignalSpy spy(app, &AgPlayerApplication::fileOpenRequested);
        const auto first = QUrl::fromLocalFile(QStringLiteral("/music/中文 曲目.flac"));
        const auto second = QUrl::fromLocalFile(QStringLiteral("/music/second.mp3"));
        QFileOpenEvent early(first);
        QFileOpenEvent earlySecond(second);
        QCoreApplication::sendEvent(app, &early);
        QCoreApplication::sendEvent(app, &earlySecond);
        QCOMPARE(spy.count(), 0);
        app->enableFileOpenDelivery();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toUrl(), first);
        QCOMPARE(spy.at(1).at(0).toUrl(), second);
        app->enableFileOpenDelivery();
        QCOMPARE(spy.count(), 2); // no replay on repeated readiness
        QFileOpenEvent running(first);
        QCoreApplication::sendEvent(app, &running);
        QCOMPARE(spy.count(), 3);
        QFileOpenEvent remote(QUrl(QStringLiteral("https://example.com/a.mp3")));
        QCoreApplication::sendEvent(app, &remote);
        QCOMPARE(spy.count(), 3);
    }
};

int main(int argc, char** argv)
{
    AgPlayerApplication app(argc, argv);
    ApplicationFileOpenTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "application_file_open_test.moc"
