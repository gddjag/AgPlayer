#include "library_model.hpp"
#include "playback_controller.hpp"
#include "video_decoder.hpp"
#include "video_playback_controller.hpp"

#include <agplayer/c_api.h>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

constexpr qint64 kQueueByteLimit = 64LL * 1024LL * 1024LL;

QString requiredFixture(const char* name)
{
    const QString path = QString::fromUtf8(qgetenv(name));
    return QFileInfo::exists(path) ? path : QString{};
}

TrackRecord makeTrack(QString id, const QString& path, bool hasAudio,
                      bool hasVideo)
{
    TrackRecord track;
    track.trackId = std::move(id);
    track.path = path;
    track.title = track.trackId;
    track.available = true;
    track.hasAudio = hasAudio;
    track.hasVideo = hasVideo;
    return track;
}

class NullCore final {
public:
    NullCore()
    {
        ag_player_config config{AG_AUDIO_BACKEND_NULL, 2'048};
        if (ag_player_create_with_config(&config, &value_) != AG_OK) {
            value_ = nullptr;
        }
    }

    ~NullCore() { ag_player_destroy(value_); }
    NullCore(const NullCore&) = delete;
    NullCore& operator=(const NullCore&) = delete;
    ag_player* get() const noexcept { return value_; }

private:
    ag_player* value_ = nullptr;
};

class BlockingVideoOpenHook final {
public:
    BlockingVideoOpenHook()
    {
        agplayer::set_video_decoder_test_hook(&invoke, this);
    }

    ~BlockingVideoOpenHook()
    {
        release();
        agplayer::set_video_decoder_test_hook(nullptr, nullptr);
    }

    BlockingVideoOpenHook(const BlockingVideoOpenHook&) = delete;
    BlockingVideoOpenHook& operator=(const BlockingVideoOpenHook&) = delete;

    bool waitUntilEntered(const std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout,
                                   [this] { return entered_; });
    }

    void release() noexcept
    {
        try {
            const std::lock_guard lock(mutex_);
            released_ = true;
            condition_.notify_all();
        } catch (...) {
        }
    }

private:
    static bool invoke(const agplayer::VideoDecoderTestPoint point,
                       void* const opaque) noexcept
    {
        return static_cast<BlockingVideoOpenHook*>(opaque)->onHook(point);
    }

    bool onHook(const agplayer::VideoDecoderTestPoint point) noexcept
    {
        if (point != agplayer::VideoDecoderTestPoint::open_entered) {
            return false;
        }
        try {
            std::unique_lock lock(mutex_);
            entered_ = true;
            condition_.notify_all();
            condition_.wait(lock, [this] { return released_; });
        } catch (...) {
        }
        return false;
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_ = false;
    bool released_ = false;
};

class UnsupportedCodecOpenHook final {
public:
    UnsupportedCodecOpenHook()
    {
        agplayer::set_video_decoder_test_hook(&invoke, this);
    }

    ~UnsupportedCodecOpenHook()
    {
        agplayer::set_video_decoder_test_hook(nullptr, nullptr);
    }

    UnsupportedCodecOpenHook(const UnsupportedCodecOpenHook&) = delete;
    UnsupportedCodecOpenHook& operator=(const UnsupportedCodecOpenHook&) = delete;

private:
    static bool invoke(const agplayer::VideoDecoderTestPoint point,
                       void*) noexcept
    {
        return point == agplayer::VideoDecoderTestPoint::codec_open_entered;
    }
};

} // namespace

class VideoPlaybackControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void audioOnlyPlaybackAllocatesNoVideoResources();
    void frameQueueAdmissionHonorsCountByteAndOversizedRules();
    void videoPlaybackUsesOneBoundedWorkerAndStopsSynchronously();
    void dismissStopsVideoResourcesWithoutChangingPlaybackState();
    void tempoAdjustedAudioClockAdvancesFramePtsAndSerial();
    void switchingVideosAdvancesGenerationAndReleasesOldFrame();
    void committedSeekDropsTheDisplayedGeneration();
    void legacyVideoIsProbedOnceAndReused();
    void legacyVideoWithAudioPersistsContainerKindAtomically();
    void legacyAudioOnlyWithVideoSuffixPersistsContainerKind();
    void legacyUnsupportedCodecPersistsVideoKindAndShowsError();
    void repeatedVideoAudioSwitchesLeaveNoResources();
    void destructionJoinsAnActiveWorker();
    void abandonedLegacyProbeCanBeClaimedAgain();
    void seekRetriesAfterVideoDecoderFailure();
    void switchingFromActiveLegacyProbeCancelsOpenAndReleasesClaim();
    void switchingFromFailedVideoToAudioClearsError();
};

void VideoPlaybackControllerTest::frameQueueAdmissionHonorsCountByteAndOversizedRules()
{
    const qint64 limit = VideoPlaybackController::MaxQueuedFrameBytes;
    QVERIFY(videoFrameQueueCanAdmit(0, 0, limit));
    QVERIFY(videoFrameQueueCanAdmit(0, 0, limit + 1));
    QVERIFY(!videoFrameQueueCanAdmit(1, limit + 1, 1));
    QVERIFY(videoFrameQueueCanAdmit(1, limit - 1, 1));
    QVERIFY(!videoFrameQueueCanAdmit(1, limit - 1, 2));
    QVERIFY(videoFrameQueueCanAdmit(2, 2, 1));
    QVERIFY(!videoFrameQueueCanAdmit(3, 3, 1));
}

void VideoPlaybackControllerTest::audioOnlyPlaybackAllocatesNoVideoResources()
{
    const QString audio = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY2(!audio.isEmpty(), "missing AGPLAYER_TEST_WAV");
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("audio"), audio, true,
                                     false)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_COMPARE(playback.currentTrackId(), QStringLiteral("audio"));
    QTest::qWait(250);
    QVERIFY(!video.visible());
    QVERIFY(!video.loading());
    QVERIFY(!video.workerRunning());
    QCOMPARE(video.queuedFrameCount(), 0);
    QCOMPARE(video.queuedFrameBytes(), qint64{0});
    QCOMPARE(video.frameSerial(), quint64{0});
}

void VideoPlaybackControllerTest::tempoAdjustedAudioClockAdvancesFramePtsAndSerial()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!videoPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("tempo-video"), videoPath,
                                     true, true)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_COMPARE(playback.state(), PlaybackController::Playing);
    QTRY_VERIFY(playback.positionMs() >= 150);
    QTRY_VERIFY(video.currentFrame() != nullptr);
    const auto first = video.currentFrame();
    QVERIFY(first != nullptr);
    const qint64 firstPts = first->ptsMs;
    const quint64 firstSerial = first->serial;

    playback.setSpeedRatio(1.5);
    QCOMPARE(playback.speedRatio(), 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(playback.positionMs() >= firstPts + 250, 1'500);
    QTRY_VERIFY_WITH_TIMEOUT(
        video.currentFrame() != nullptr
            && video.currentFrame()->ptsMs > firstPts
            && video.currentFrame()->serial > firstSerial,
        1'500);
    const auto advanced = video.currentFrame();
    QVERIFY(advanced != nullptr);
    QVERIFY(qAbs(playback.positionMs() - advanced->ptsMs) <= 150);
}

void VideoPlaybackControllerTest::switchingVideosAdvancesGenerationAndReleasesOldFrame()
{
    const QString source = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!source.isEmpty());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString firstPath = temporary.filePath(QStringLiteral("first.avi"));
    const QString secondPath = temporary.filePath(QStringLiteral("second.avi"));
    QVERIFY(QFile::copy(source, firstPath));
    QVERIFY(QFile::copy(source, secondPath));
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord firstTrack = makeTrack(QStringLiteral("first-video"),
                                             firstPath, true, true);
    const TrackRecord secondTrack = makeTrack(QStringLiteral("second-video"),
                                              secondPath, true, true);
    QVERIFY(library.append(firstTrack));
    QVERIFY(library.append(secondTrack));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_COMPARE(playback.currentTrackId(), firstTrack.trackId);
    QTRY_VERIFY(video.currentFrame() != nullptr);
    auto oldFrame = video.currentFrame();
    QVERIFY(oldFrame != nullptr);
    const quint64 oldGeneration = oldFrame->generation;
    std::weak_ptr<const VideoFrameSnapshot> releasedFrame = oldFrame;
    oldFrame.reset();

    playback.playRow(1);
    QTRY_COMPARE(playback.currentTrackId(), secondTrack.trackId);
    QTRY_VERIFY(video.currentFrame() != nullptr
                && video.currentFrame()->generation > oldGeneration);
    QTRY_VERIFY(releasedFrame.expired());
    QVERIFY(video.workerRunning());
    QVERIFY(video.queuedFrameCount() <= VideoPlaybackController::MaxQueuedFrames);
}

void VideoPlaybackControllerTest::videoPlaybackUsesOneBoundedWorkerAndStopsSynchronously()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY2(!videoPath.isEmpty(), "missing video-with-audio fixture");
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("video"), videoPath, true,
                                     true)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_COMPARE(playback.currentTrackId(), QStringLiteral("video"));
    QTRY_VERIFY(video.workerRunning());
    QTRY_VERIFY(video.visible());
    QTRY_VERIFY(video.frameSerial() > 0);

    playback.pause();
    QTRY_COMPARE(playback.state(), PlaybackController::Paused);
    QTRY_VERIFY(video.queuedFrameCount() > 0);
    QTest::qWait(150);
    QVERIFY(video.queuedFrameCount() <= 3);
    QVERIFY(video.queuedFrameBytes() <= kQueueByteLimit);
    const int pausedDepth = video.queuedFrameCount();
    QTest::qWait(150);
    QCOMPARE(video.queuedFrameCount(), pausedDepth);

    QElapsedTimer releaseTimer;
    releaseTimer.start();
    playback.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!video.workerRunning(), 2'000);
    QVERIFY2(releaseTimer.elapsed() < 500,
             qPrintable(QStringLiteral("video worker release took %1 ms")
                            .arg(releaseTimer.elapsed())));
    QCOMPARE(video.queuedFrameCount(), 0);
    QCOMPARE(video.queuedFrameBytes(), qint64{0});
    QVERIFY(!video.visible());
}

void VideoPlaybackControllerTest::dismissStopsVideoResourcesWithoutChangingPlaybackState()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY2(!videoPath.isEmpty(), "missing video-with-audio fixture");
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("dismiss-video"), videoPath,
                                     true, true)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(video.workerRunning());
    QTRY_VERIFY(video.visible());
    QTRY_VERIFY(video.frameSerial() > 0);

    video.dismiss();

    QVERIFY(!video.workerRunning());
    QVERIFY(!video.visible());
    QCOMPARE(video.queuedFrameCount(), 0);
    QCOMPARE(video.queuedFrameBytes(), qint64{0});
    QVERIFY(playback.state() != PlaybackController::Stopped);
}

void VideoPlaybackControllerTest::committedSeekDropsTheDisplayedGeneration()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!videoPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("seek-video"), videoPath,
                                     true, true)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(video.frameSerial() > 0);
    playback.pause();
    QTRY_COMPARE(playback.state(), PlaybackController::Paused);
    const auto before = video.currentFrame();
    QVERIFY(before != nullptr);
    const quint64 oldGeneration = before->generation;
    QSignalSpy committed(&playback, &PlaybackController::seekCommitted);
    playback.seek(1'000);
    QCOMPARE(committed.count(), 1);
    QTRY_VERIFY(video.currentFrame() != nullptr);
    QTRY_VERIFY(video.currentFrame()->generation > oldGeneration);
    QVERIFY(video.currentFrame()->ptsMs >= 900);
}

void VideoPlaybackControllerTest::legacyVideoIsProbedOnceAndReused()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_ONLY");
    QVERIFY(!videoPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    TrackRecord legacy = makeTrack(QStringLiteral("legacy-video"), videoPath,
                                   false, false);
    QVERIFY(!legacy.metadataProbeAttempted);
    QVERIFY(library.append(legacy));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(video.workerRunning());
    QTRY_VERIFY(video.visible());
    QTRY_VERIFY(library.recordForId(legacy.trackId)->metadataProbeAttempted);
    QTRY_VERIFY(video.frameSerial() > 0);
    playback.stop();
    QTRY_VERIFY(!video.workerRunning());

    const quint64 firstSerial = video.frameSerial();
    playback.play();
    QTRY_VERIFY(video.workerRunning());
    QTRY_VERIFY(video.visible());
    QTRY_VERIFY(video.frameSerial() > firstSerial);
    QVERIFY(library.recordForId(legacy.trackId)->metadataProbeAttempted);
}

void VideoPlaybackControllerTest::legacyVideoWithAudioPersistsContainerKindAtomically()
{
    const QString path = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!path.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord legacy = makeTrack(QStringLiteral("legacy-av"), path,
                                         false, false);
    QVERIFY(library.append(legacy));
    QSignalSpy changed(&library, &QAbstractItemModel::dataChanged);
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(library.recordForId(legacy.trackId)->metadataProbeAttempted);
    const TrackRecord* const current = library.recordForId(legacy.trackId);
    QVERIFY(current != nullptr);
    QVERIFY(current->hasAudio);
    QVERIFY(current->hasVideo);
    QTRY_VERIFY(video.currentFrame() != nullptr);

    int probeChangeCount = 0;
    QList<int> probeRoles;
    for (const QList<QVariant>& arguments : changed) {
        const QList<int> roles = qvariant_cast<QList<int>>(arguments.at(2));
        if (roles.contains(LibraryModel::MetadataProbeAttemptedRole)) {
            ++probeChangeCount;
            probeRoles = roles;
        }
    }
    QCOMPARE(probeChangeCount, 1);
    QVERIFY(probeRoles.contains(LibraryModel::HasAudioRole));
    QVERIFY(probeRoles.contains(LibraryModel::HasVideoRole));
}

void VideoPlaybackControllerTest::legacyAudioOnlyWithVideoSuffixPersistsContainerKind()
{
    const QString source = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY(!source.isEmpty());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("audio.avi"));
    QVERIFY(QFile::copy(source, path));
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord legacy = makeTrack(QStringLiteral("legacy-audio"), path,
                                         false, false);
    QVERIFY(library.append(legacy));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(library.recordForId(legacy.trackId)->metadataProbeAttempted);
    QTRY_VERIFY(!video.workerRunning());
    const TrackRecord* const current = library.recordForId(legacy.trackId);
    QVERIFY(current != nullptr);
    QVERIFY(current->hasAudio);
    QVERIFY(!current->hasVideo);
    QVERIFY(!video.visible());
    QVERIFY(video.errorMessage().isEmpty());
    QVERIFY(!library.beginMetadataProbe(legacy.trackId).has_value());

    playback.stop();
    playback.play();
    QTest::qWait(100);
    QVERIFY(!video.workerRunning());
}

void VideoPlaybackControllerTest::legacyUnsupportedCodecPersistsVideoKindAndShowsError()
{
    const QString path = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!path.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord legacy = makeTrack(QStringLiteral("unsupported-video"),
                                         path, false, false);
    QVERIFY(library.append(legacy));
    QSignalSpy changed(&library, &QAbstractItemModel::dataChanged);
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);
    UnsupportedCodecOpenHook hook;

    playback.playRow(0);
    QTRY_VERIFY(library.recordForId(legacy.trackId)->metadataProbeAttempted);
    const TrackRecord* const current = library.recordForId(legacy.trackId);
    QVERIFY(current != nullptr);
    QVERIFY(current->hasAudio);
    QVERIFY(current->hasVideo);
    QTRY_VERIFY(!video.errorMessage().isEmpty());
    QVERIFY(video.visible());
    QTRY_VERIFY(!video.workerRunning());
    const auto probeChangeCount = [&changed] {
        int count = 0;
        for (const QList<QVariant>& arguments : changed) {
            const QList<int> roles =
                qvariant_cast<QList<int>>(arguments.at(2));
            if (roles.contains(LibraryModel::MetadataProbeAttemptedRole)) {
                ++count;
            }
        }
        return count;
    };
    QCOMPARE(probeChangeCount(), 1);
    QVERIFY(!library.beginMetadataProbe(legacy.trackId).has_value());

    playback.stop();
    playback.play();
    QTRY_VERIFY(!video.errorMessage().isEmpty());
    QTRY_VERIFY(!video.workerRunning());
    QCOMPARE(probeChangeCount(), 1);
}

void VideoPlaybackControllerTest::repeatedVideoAudioSwitchesLeaveNoResources()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    const QString audioPath = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY(!videoPath.isEmpty());
    QVERIFY(!audioPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("video"), videoPath, true,
                                     true)));
    QVERIFY(library.append(makeTrack(QStringLiteral("audio"), audioPath, true,
                                     false)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(video.workerRunning());
    playback.setSpeedRatio(1.25);
    QTRY_COMPARE(playback.speedRatio(), 1.25);
    QVERIFY(video.visible());
    playback.next();
    QTRY_COMPARE(playback.currentTrackId(), QStringLiteral("audio"));
    QTRY_VERIFY(!video.workerRunning());
    playback.previous();
    QTRY_COMPARE(playback.currentTrackId(), QStringLiteral("video"));
    QTRY_VERIFY(video.workerRunning());

    for (int iteration = 0; iteration < 50; ++iteration) {
        playback.playRow(0);
        QTRY_COMPARE_WITH_TIMEOUT(playback.currentTrackId(),
                                  QStringLiteral("video"), 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(video.workerRunning(), 1'000);
        playback.playRow(1);
        QTRY_COMPARE_WITH_TIMEOUT(playback.currentTrackId(),
                                  QStringLiteral("audio"), 1'000);
        QTRY_VERIFY_WITH_TIMEOUT(!video.workerRunning(), 1'000);
        QCOMPARE(video.queuedFrameCount(), 0);
        QCOMPARE(video.queuedFrameBytes(), qint64{0});
    }
}

void VideoPlaybackControllerTest::destructionJoinsAnActiveWorker()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_WITH_AUDIO");
    QVERIFY(!videoPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("video"), videoPath, true,
                                     true)));
    PlaybackController playback(core.get(), &library);
    auto video = std::make_unique<VideoPlaybackController>(&library, &playback);
    playback.playRow(0);
    QTRY_VERIFY(video->workerRunning());
    QElapsedTimer timer;
    timer.start();
    video.reset();
    QVERIFY2(timer.elapsed() < 500,
             qPrintable(QStringLiteral("destructor took %1 ms")
                            .arg(timer.elapsed())));
}

void VideoPlaybackControllerTest::abandonedLegacyProbeCanBeClaimedAgain()
{
    const QString videoPath = requiredFixture("AGPLAYER_TEST_VIDEO_ONLY");
    QVERIFY(!videoPath.isEmpty());
    LibraryModel library;
    const TrackRecord legacy = makeTrack(QStringLiteral("legacy"), videoPath,
                                         false, false);
    QVERIFY(library.append(legacy));
    const auto first = library.beginMetadataProbe(legacy.trackId);
    QVERIFY(first.has_value());
    QVERIFY(library.abandonMetadataProbe(*first));
    QVERIFY(library.beginMetadataProbe(legacy.trackId).has_value());
}

void VideoPlaybackControllerTest::seekRetriesAfterVideoDecoderFailure()
{
    const QString audioPath = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY(!audioPath.isEmpty());
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    QVERIFY(library.append(makeTrack(QStringLiteral("bad-video"), audioPath,
                                     true, true)));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);
    playback.playRow(0);
    QTRY_VERIFY(!video.errorMessage().isEmpty());
    QTRY_VERIFY(!video.workerRunning());
    QVERIFY(!video.loading());

    playback.seek(1'000);
    QTRY_VERIFY_WITH_TIMEOUT(!video.loading(), 2'000);
    QVERIFY(!video.errorMessage().isEmpty());
    QVERIFY(!video.workerRunning());
}

void VideoPlaybackControllerTest::switchingFromActiveLegacyProbeCancelsOpenAndReleasesClaim()
{
    const QString audioPath = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY(!audioPath.isEmpty());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString disguisedAudio = temporary.filePath(
        QStringLiteral("legacy-audio.avi"));
    QVERIFY(QFile::copy(audioPath, disguisedAudio));

    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord legacy = makeTrack(QStringLiteral("legacy-audio"),
                                         disguisedAudio, true, false);
    QVERIFY(library.append(legacy));
    TrackRecord audio = makeTrack(QStringLiteral("audio"), audioPath,
                                  true, false);
    audio.metadataProbeAttempted = true;
    QVERIFY(library.append(audio));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);
    BlockingVideoOpenHook hook;

    playback.playRow(0);
    QTRY_COMPARE(playback.currentTrackId(), legacy.trackId);
    QVERIFY(!library.recordForId(legacy.trackId)->metadataProbeAttempted);
    QTRY_VERIFY(video.workerRunning());
    QVERIFY2(hook.waitUntilEntered(std::chrono::seconds(2)),
             "legacy probe did not use the cancellable video decoder");

    std::atomic_bool switchStarted{false};
    std::thread releaseAfterOverlap([&] {
        while (!switchStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hook.release();
    });
    QElapsedTimer timer;
    timer.start();
    switchStarted.store(true, std::memory_order_release);
    playback.playRow(1);
    QTRY_COMPARE(playback.currentTrackId(), audio.trackId);
    releaseAfterOverlap.join();

    QVERIFY2(timer.elapsed() < 500,
             qPrintable(QStringLiteral("legacy cancellation took %1 ms")
                            .arg(timer.elapsed())));
    QVERIFY(!video.workerRunning());
    QCOMPARE(video.queuedFrameCount(), 0);
    QCOMPARE(video.queuedFrameBytes(), qint64{0});
    QVERIFY(!library.recordForId(legacy.trackId)->metadataProbeAttempted);
    const auto retried = library.beginMetadataProbe(legacy.trackId);
    QVERIFY(retried.has_value());
    QVERIFY(library.abandonMetadataProbe(*retried));
}

void VideoPlaybackControllerTest::switchingFromFailedVideoToAudioClearsError()
{
    const QString source = requiredFixture("AGPLAYER_TEST_WAV");
    QVERIFY(!source.isEmpty());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString audioPath = temporary.filePath(QStringLiteral("audio.wav"));
    QVERIFY(QFile::copy(source, audioPath));
    NullCore core;
    QVERIFY(core.get() != nullptr);
    LibraryModel library;
    const TrackRecord failedVideo = makeTrack(QStringLiteral("failed-video"),
                                              source, true, true);
    TrackRecord audio = makeTrack(QStringLiteral("audio"), audioPath,
                                  true, false);
    audio.metadataProbeAttempted = true;
    QVERIFY(library.append(failedVideo));
    QVERIFY(library.append(audio));
    PlaybackController playback(core.get(), &library);
    VideoPlaybackController video(&library, &playback);

    playback.playRow(0);
    QTRY_VERIFY(!video.errorMessage().isEmpty());
    QTRY_VERIFY(!video.workerRunning());
    playback.playRow(1);
    QTRY_COMPARE(playback.currentTrackId(), audio.trackId);
    QTRY_VERIFY(!video.workerRunning());
    QVERIFY(!video.visible());
    QVERIFY(video.errorMessage().isEmpty());
}

QTEST_MAIN(VideoPlaybackControllerTest)
#include "video_playback_controller_test.moc"
