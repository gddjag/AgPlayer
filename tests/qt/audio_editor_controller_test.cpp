#include "audio_editor/audio_editor_controller.hpp"

#include <QtTest>

class AudioEditorControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyDocumentDisablesDocumentActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.action(QStringLiteral("editor.open"))->enabled);
        QVERIFY(controller.actionEnabled(QStringLiteral("editor.open")));
        QVERIFY(!controller.action(QStringLiteral("editor.save"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.export"))->enabled);
    }

    void readyDocumentEnablesOnlyApplicableActions()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(controller.hasDocument());
        QVERIFY(controller.action(QStringLiteral("editor.save"))->enabled);
        QVERIFY(controller.action(QStringLiteral("editor.export"))->enabled);
        QVERIFY(!controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(controller.setSelection(24'000, 48'000));
        QVERIFY(controller.action(QStringLiteral("editor.cut"))->enabled);
        QVERIFY(controller.action(QStringLiteral("editor.cropToSelection"))->enabled);
    }

    void editActionsMutateThroughOneControllerSeam()
    {
        AudioEditorController controller;
        QVERIFY(controller.createUntitledDocument(48'000, 2, 96'000));
        QVERIFY(controller.setSelection(24'000, 48'000));
        QVERIFY(controller.triggerAction(QStringLiteral("editor.deleteSelection")));
        QCOMPARE(controller.totalFrames(), qint64{72'000});
        QVERIFY(controller.modified());
        QVERIFY(controller.action(QStringLiteral("editor.undo"))->enabled);
        QVERIFY(controller.triggerAction(QStringLiteral("editor.undo")));
        QCOMPARE(controller.totalFrames(), qint64{96'000});
        QVERIFY(controller.action(QStringLiteral("editor.redo"))->enabled);
    }

    void exposesStableCommandOrder()
    {
        AudioEditorController controller;
        const QStringList expected{
            "editor.open", "editor.newRecording", "editor.save",
            "editor.undo", "editor.redo", "editor.cut", "editor.copy",
            "editor.paste", "editor.deleteSelection", "editor.cropToSelection",
            "editor.silenceSelection", "editor.fadeIn", "editor.fadeOut",
            "editor.moreMenu", "editor.export"};
        QCOMPARE(controller.actions()->ids(), expected);
    }
};

QTEST_APPLESS_MAIN(AudioEditorControllerTest)

#include "audio_editor_controller_test.moc"
