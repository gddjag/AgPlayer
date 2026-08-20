#include "audio_editor/audio_event.hpp"

#include <QtTest>

#include <memory>
#include <type_traits>

using namespace agplayer::editor;

class AudioEventTest final : public QObject {
    Q_OBJECT

private:
    static std::shared_ptr<const AudioSource> source(const SampleFrame frames = 1'000)
    {
        return std::make_shared<const AudioSource>(AudioSource{
            "fixture.wav", 48'000, 2, frames});
    }

    static AudioEvent event(const SampleFrame frames = 1'000)
    {
        return {1, source(frames), 100, 900, 200};
    }

private slots:
    void validEventSharesAnImmutableSourceWithoutPcmOwnership()
    {
        const auto shared_source = source();
        const AudioEvent left{1, shared_source, 0, 500, 0};
        const AudioEvent right{2, shared_source, 500, 1'000, 750};

        QCOMPARE(left.source.get(), right.source.get());
        static_assert(std::is_const_v<std::remove_reference_t<
            decltype(*left.source)>>);
        QVERIFY(isValid(left));
        QVERIFY(isValid(right));
    }

    void rejectsInvalidSourceBoundsAndNegativeTimeline()
    {
        auto candidate = event();
        candidate.sourceStart = 9;
        candidate.sourceEnd = 8;
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.sourceStart = -1;
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.sourceEnd = 1'001;
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.timelineStart = -1;
        QVERIFY(!isValid(candidate));
    }

    void rejectsFadesThatExceedAudibleFrames()
    {
        auto candidate = event();
        candidate.fadeIn = 401;
        candidate.fadeOut = 400;
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.fadeIn = -1;
        QVERIFY(!isValid(candidate));
    }

    void rejectsInvalidEnvelope()
    {
        auto candidate = event();
        candidate.envelope = {{200, 0.5F}, {100, 1.0F}};
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.envelope = {{800, 0.5F}};
        QVERIFY(!isValid(candidate));

        candidate = event();
        candidate.envelope.assign(kMaxEnvelopePoints + 1, {0, 1.0F});
        QVERIFY(!isValid(candidate));
    }

    void keepsTwoHoursOfSampleFramesAsInt64()
    {
        static_assert(sizeof(SampleFrame) == sizeof(std::int64_t));
        constexpr SampleFrame two_hours_at_384khz = 2'764'800'000LL;
        const AudioEvent candidate{1, source(two_hours_at_384khz), 0,
                                   two_hours_at_384khz, 0};

        QVERIFY(isValid(candidate));
        QCOMPARE(audibleFrames(candidate), two_hours_at_384khz);
    }
};

QTEST_APPLESS_MAIN(AudioEventTest)

#include "audio_event_test.moc"
