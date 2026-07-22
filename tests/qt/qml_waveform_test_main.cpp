#include "waveform_item.hpp"

#include <QQmlEngine>
#include <QtQml/qqml.h>
#include <QtQuickTest/quicktest.h>

class QmlWaveformSetup final : public QObject {
    Q_OBJECT

public slots:
    void applicationAvailable() {
        qmlRegisterType<WaveformItem>("AgPlayer", 1, 0, "WaveformItem");
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_waveform, QmlWaveformSetup)

#include "qml_waveform_test_main.moc"
