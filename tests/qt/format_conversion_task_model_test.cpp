#include "format_conversion_filter_model.hpp"
#include "format_conversion_task_model.hpp"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

class FormatConversionTaskModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesNineColumnsAndStableRoles();
    void keepsIdsStableAndChecksVisibleRows();
    void visibleCountsNotifyWhenChecksChangeThroughSourceModel();
    void filtersSearchStatusAndFormatWithinBudget();
};

namespace {

QVariantMap task(const QString& id,
                 const QString& name,
                 const QString& source,
                 const QString& output,
                 const QString& status,
                 const bool checked = true)
{
    return {{QStringLiteral("taskId"), id},
            {QStringLiteral("checked"), checked},
            {QStringLiteral("fileName"), name},
            {QStringLiteral("path"), QStringLiteral("C:/music/") + name},
            {QStringLiteral("sourceFormat"), source},
            {QStringLiteral("durationMs"), 1000},
            {QStringLiteral("sampleRate"), 44100},
            {QStringLiteral("bitRate"), 192000},
            {QStringLiteral("channelLayout"), QStringLiteral("stereo")},
            {QStringLiteral("sampleFormat"), QStringLiteral("s16")},
            {QStringLiteral("outputFormat"), output},
            {QStringLiteral("status"), status},
            {QStringLiteral("stage"), status},
            {QStringLiteral("progress"), 0.25},
            {QStringLiteral("errorSummary"), QString()},
            {QStringLiteral("resolvedProfile"),
             output + QStringLiteral(" 44.1kHz")}};
}

} // namespace

void FormatConversionTaskModelTest::exposesNineColumnsAndStableRoles()
{
    FormatConversionTaskModel model;
    QCOMPARE(model.columnCount(), 9);
    const auto roles = model.roleNames();
    QCOMPARE(roles.value(FormatConversionTaskModel::TaskIdRole),
             QByteArray("taskId"));
    QCOMPARE(roles.value(FormatConversionTaskModel::CheckedRole),
             QByteArray("checked"));
    QCOMPARE(roles.value(FormatConversionTaskModel::ProgressRole),
             QByteArray("progress"));

    model.appendTask(task(QStringLiteral("a"), QStringLiteral("song.wav"),
                          QStringLiteral("wav"), QStringLiteral("mp3"),
                          QStringLiteral("Ready")));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0),
                        FormatConversionTaskModel::TaskIdRole).toString(),
             QStringLiteral("a"));
}

void FormatConversionTaskModelTest::keepsIdsStableAndChecksVisibleRows()
{
    FormatConversionTaskModel model;
    model.appendTask(task(QStringLiteral("a"), QStringLiteral("alpha.wav"),
                          QStringLiteral("wav"), QStringLiteral("mp3"),
                          QStringLiteral("Ready")));
    model.appendTask(task(QStringLiteral("b"), QStringLiteral("beta.flac"),
                          QStringLiteral("flac"), QStringLiteral("mp3"),
                          QStringLiteral("Done"), false));
    model.appendTask(task(QStringLiteral("c"), QStringLiteral("gamma.wav"),
                          QStringLiteral("wav"), QStringLiteral("flac"),
                          QStringLiteral("Ready")));
    model.removeTasks({QStringLiteral("b")});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.taskIdAt(1), QStringLiteral("c"));

    FormatConversionFilterModel proxy;
    proxy.setSourceModel(&model);
    proxy.setStatusFilter(QStringLiteral("Ready"));
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setAllVisibleChecked(false);
    QCOMPARE(model.checkedCount(), 0);
    proxy.setAllVisibleChecked(true);
    QCOMPARE(model.checkedCount(), 2);
}

void FormatConversionTaskModelTest::visibleCountsNotifyWhenChecksChangeThroughSourceModel()
{
    FormatConversionTaskModel model;
    model.appendTask(task(QStringLiteral("done"), QStringLiteral("done.wav"),
                          QStringLiteral("wav"), QStringLiteral("flac"),
                          QStringLiteral("Done"), false));
    model.appendTask(task(QStringLiteral("error"), QStringLiteral("error.wav"),
                          QStringLiteral("wav"), QStringLiteral("flac"),
                          QStringLiteral("Error"), true));

    FormatConversionFilterModel proxy;
    proxy.setSourceModel(&model);
    proxy.setStatusFilter(QStringLiteral("Done"));
    QCOMPARE(proxy.visibleCount(), 1);
    QCOMPARE(proxy.visibleCheckedCount(), 0);

    QSignalSpy visibleCountsChanged(
        &proxy, &FormatConversionFilterModel::visibleCountsChanged);
    model.setChecked(QStringLiteral("done"), true);
    QVERIFY(visibleCountsChanged.count() > 0);
    QCOMPARE(proxy.visibleCheckedCount(), 1);
}

void FormatConversionTaskModelTest::filtersSearchStatusAndFormatWithinBudget()
{
    FormatConversionTaskModel model;
    for (int index = 0; index < 1000; ++index) {
        model.appendTask(task(
            QString::number(index),
            QStringLiteral("track-%1-%2.wav")
                .arg(index)
                .arg(index == 777 ? QStringLiteral("needle")
                                  : QStringLiteral("ordinary")),
            QStringLiteral("wav"),
            index % 2 == 0 ? QStringLiteral("mp3") : QStringLiteral("flac"),
            index % 3 == 0 ? QStringLiteral("Done") : QStringLiteral("Ready")));
    }

    FormatConversionFilterModel proxy;
    proxy.setSourceModel(&model);
    QElapsedTimer timer;
    timer.start();
    proxy.setQuery(QStringLiteral("needle"));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(timer.elapsed() < 100);
    QCOMPARE(proxy.sourceTaskId(0), QStringLiteral("777"));

    proxy.setQuery({});
    proxy.setStatusFilter(QStringLiteral("Done"));
    proxy.setFormatFilter(QStringLiteral("mp3"));
    QVERIFY(proxy.rowCount() > 0);
    for (int row = 0; row < proxy.rowCount(); ++row) {
        const QModelIndex item = proxy.index(row, 0);
        QCOMPARE(item.data(FormatConversionTaskModel::StatusRole).toString(),
                 QStringLiteral("Done"));
        QCOMPARE(item.data(FormatConversionTaskModel::OutputFormatRole).toString(),
                 QStringLiteral("mp3"));
    }
}

QTEST_GUILESS_MAIN(FormatConversionTaskModelTest)

#include "format_conversion_task_model_test.moc"
