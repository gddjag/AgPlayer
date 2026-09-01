#include "library_model.hpp"
#include "playback_controller.hpp"
#include "video_playback_controller.hpp"

#include <agplayer/c_api.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <memory>

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

} // namespace

class VideoPlaybackControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void audioOnlyPlaybackAllocatesNoVideoResources();
    void videoPlaybackUsesOneBoundedWorkerAndStopsSynchronously();
    void committedSeekDropsTheDisplayedGeneration();
    void legacyVideoIsProbedOnceAndReused();
    void repeatedVideoAudioSwitchesLeaveNoResources();
    void destructionJoinsAnActiveWorker();
    void abandonedLegacyProbeCanBeClaimedAgain();
    void seekRetriesAfterVideoDecoderFailure();
};

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

QTEST_MAIN(VideoPlaybackControllerTest)
#include "video_playback_controller_test.moc"
