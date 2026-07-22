#include "library_model.hpp"
#include "qml_registration.hpp"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopedPointer>
#include <QTest>

class QmlRegistrationTest final : public QObject {
    Q_OBJECT

private slots:
    void libraryModelCanBeCreatedFromAgPlayerImport();
};

void QmlRegistrationTest::libraryModelCanBeCreatedFromAgPlayerImport()
{
    registerAgPlayerQmlTypes();
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import AgPlayer 1.0\nLibraryModel {}", QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    QVERIFY(qobject_cast<LibraryModel*>(object.data()) != nullptr);
}

QTEST_GUILESS_MAIN(QmlRegistrationTest)
#include "qml_registration_test.moc"
