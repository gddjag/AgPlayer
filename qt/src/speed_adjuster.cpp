#include "speed_adjuster.hpp"

#include <agplayer/c_api.h>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

SpeedAdjuster::SpeedAdjuster(QObject* parent)
    : QObject(parent)
{
}

SpeedAdjuster::~SpeedAdjuster()
{
    // If a background task is still running, the token is owned by the task
    // lambda and destroyed there. Just ensure we don't dangle.
    token_.store(nullptr, std::memory_order_release);
}

double SpeedAdjuster::progress() const noexcept
{
    return progress_.load(std::memory_order_acquire);
}

bool SpeedAdjuster::busy() const noexcept
{
    return busy_.load(std::memory_order_acquire);
}

QString SpeedAdjuster::inputFileName() const noexcept
{
    return inputFileName_;
}

bool SpeedAdjuster::hasInput() const noexcept
{
    return !inputPath_.isEmpty();
}

void SpeedAdjuster::setBusy(bool value)
{
    busy_.store(value, std::memory_order_release);
    emit busyChanged();
}

void SpeedAdjuster::setProgress(double value)
{
    progress_.store(value, std::memory_order_release);
    emit progressChanged();
}

void SpeedAdjuster::loadFile(const QUrl& url)
{
    if (busy_.load(std::memory_order_acquire)) return;

    inputPath_ = url.toLocalFile();
    inputFileName_ = QFileInfo(inputPath_).fileName();
    emit inputFileChanged();
}

QString SpeedAdjuster::computeOutputPath(const QString& inputPath,
                                         const QString& outputDir) const
{
    const QFileInfo info(inputPath);
    const QString dir = outputDir.isEmpty() ? info.absolutePath() : outputDir;
    const QString baseName = info.completeBaseName();
    const QString ext = info.suffix();

    QString candidate = dir + QStringLiteral("/") + baseName
                        + QStringLiteral("_speed.") + ext;
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QStringLiteral("/") + baseName
                    + QStringLiteral("_speed_") + QString::number(counter)
                    + QStringLiteral(".") + ext;
        ++counter;
    }
    return candidate;
}

void SpeedAdjuster::start(double speedRatio, const QString& outputDir)
{
    if (busy_.load(std::memory_order_acquire)) return;
    if (inputPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("No input file loaded"));
        return;
    }
    if (speedRatio < 0.5 || speedRatio > 2.0) {
        emit errorOccurred(QStringLiteral("Speed ratio out of range (0.5..2.0)"));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    // Create the cancel token on the main thread so cancel() can flip it.
    ag_cancel_token* token = ag_cancel_token_create();
    token_.store(token, std::memory_order_release);

    const QString outputPath = computeOutputPath(inputPath_, outputDir);
    const QString inputPath = inputPath_;
    // speed_ratio -> tempo_ratio: speed_ratio=2.0 (2x faster) means
    // tempo_ratio=0.5 (OLA compress to half length, preserving pitch).
    const double tempoRatio = 1.0 / speedRatio;

    auto* watcher = new QFutureWatcher<int>(this);
    connect(watcher, &QFutureWatcher<int>::finished, this,
        [this, watcher, outputPath]() {
            watcher->deleteLater();
            ag_cancel_token* t = token_.exchange(nullptr,
                std::memory_order_acq_rel);
            if (t) ag_cancel_token_destroy(t);
            const int result = watcher->result();
            setBusy(false);
            if (result == AG_OK) {
                setProgress(1.0);
                emit speedAdjustCompleted(outputPath);
            } else if (result == AG_CANCELLED) {
                setProgress(0.0);
                emit errorOccurred(QStringLiteral("Speed adjust cancelled"));
            } else {
                emit errorOccurred(
                    QStringLiteral("Speed adjust failed (error %1)").arg(result));
            }
        });

    auto doAdjust = [this, inputPath, outputPath, tempoRatio, token]() -> int
    {
        const QByteArray inputUtf8 = inputPath.toUtf8();
        const QByteArray outputUtf8 = outputPath.toUtf8();

        auto callback = [](float frac, void* userData) {
            auto* self = static_cast<SpeedAdjuster*>(userData);
            if (self) {
                self->progress_.store(frac, std::memory_order_release);
                emit self->progressChanged();
            }
        };

        const ag_result result = ag_pitch_shift(
            inputUtf8.constData(),
            outputUtf8.constData(),
            0,                  // pitch_cents = 0 (no pitch change)
            1,                  // keep_tempo = true (preserve pitch)
            tempoRatio,         // tempo_ratio = 1/speed_ratio
            token,
            callback,
            this);

        return static_cast<int>(result);
    };

    QFuture<int> future = QtConcurrent::run(doAdjust);
    watcher->setFuture(future);
}

void SpeedAdjuster::cancel()
{
    ag_cancel_token* t = token_.load(std::memory_order_acquire);
    if (t) {
        ag_cancel_token_cancel(t);
    }
}

void SpeedAdjuster::clear()
{
    if (busy_.load(std::memory_order_acquire)) return;
    inputPath_.clear();
    inputFileName_.clear();
    setProgress(0.0);
    emit inputFileChanged();
}
