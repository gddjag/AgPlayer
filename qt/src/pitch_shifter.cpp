#include "pitch_shifter.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

PitchShifter::PitchShifter(QObject* parent)
    : QObject(parent)
{
}

double PitchShifter::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool PitchShifter::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

QString PitchShifter::inputFileName() const noexcept
{
    return inputFileName_;
}

bool PitchShifter::hasInput() const noexcept
{
    return !inputPath_.isEmpty();
}

void PitchShifter::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void PitchShifter::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void PitchShifter::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) return;

    inputPath_ = url.toLocalFile();
    inputFileName_ = QFileInfo(inputPath_).fileName();
    emit inputFileChanged();
}

QString PitchShifter::computeOutputPath(const QString& inputPath,
                                        const QString& outputDir) const
{
    const QFileInfo info(inputPath);
    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();
    const QString ext = info.suffix();

    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral("_pitched.") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_pitched_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void PitchShifter::start(int pitchCents, bool keepTempo, double tempoRatio,
                         const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }

    cancelFlag_.store(false, std::memory_order_release);
    setBusy(true);
    setProgress(0.0);

    const QString outputPath = computeOutputPath(inputPath_, outputDir);
    const QString inputPath = inputPath_;

    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher->deleteLater();
            const int result = watcher->result();
            setBusy(false);
            if (result == AG_OK) {
                setProgress(1.0);
                emit pitchShiftCompleted(outputPath);
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Pitch shift cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Pitch shift failed (error %1)").arg(result));
            }
        });

    auto doShift = [this, inputPath, outputPath, pitchCents, keepTempo, tempoRatio]()
        -> int
    {
        ag_cancel_token* token = ag_cancel_token_create();
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<PitchShifter*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        const ag_result result = ag_pitch_shift(
            inputUtf8.constData(),
            outputUtf8.constData(),
            pitchCents,
            keepTempo ? 1 : 0,
            tempoRatio,
            token,
            callback,
            this);

        ag_cancel_token_destroy(token);
        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doShift);
    watcher->setFuture(future);
}

void PitchShifter::cancel()
{
    cancelFlag_.store(true, std::memory_order_release);
}

void PitchShifter::clear()
{
    if (busy_.load(std::memory_order_acquire)) return;
    inputPath_.clear();
    inputFileName_.clear();
    setProgress(0.0);
    emit inputFileChanged();
}
