#include "qml_registration.hpp"

#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class QmlWaveformSetup final : public QObject {
    Q_OBJECT

public slots:
    void applicationAvailable() { registerAgPlayerQmlTypes(); }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_waveform, QmlWaveformSetup)

#include "qml_waveform_test_main.moc"
