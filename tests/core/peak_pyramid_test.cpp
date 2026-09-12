#include "audio_editor/peak_pyramid.hpp"

#include <QtTest>

using namespace agplayer::editor;

class PeakPyramidTest final : public QObject {
    Q_OBJECT

private slots:
    void choosesCoarsestUsefulLevelWithoutLosingExtrema()
    {
        const std::vector<std::vector<PeakBucket>> channels{{
            {-0.1F, 0.2F}, {-0.8F, 0.4F}, {-0.2F, 0.9F}, {-0.3F, 0.1F},
            {-1.0F, 0.5F}, {-0.4F, 0.7F}, {-0.6F, 0.3F}, {-0.2F, 1.0F}}};
        const auto pyramid = PeakPyramid::fromBaseBuckets(channels, 100, 800);

        const auto peaks = pyramid.read(0, 0, 800, 2);
        QCOMPARE(peaks.size(), std::size_t{4});
        QCOMPARE(peaks.front().minimum, -0.8F);
        QCOMPARE(peaks.front().maximum, 0.4F);
        QCOMPARE(peaks.back().minimum, -0.6F);
        QCOMPARE(peaks.back().maximum, 1.0F);
    }

    void rangeReadIsBoundedByPixelWidthForLongDocuments()
    {
        std::vector<PeakBucket> base(16'384, PeakBucket{-0.5F, 0.5F});
        const auto pyramid = PeakPyramid::fromBaseBuckets(
            {std::move(base)}, 21'094, SampleFrame{345'600'000});

        const auto peaks = pyramid.read(0, 0, 345'600'000, 1'920);
        QVERIFY(peaks.size() <= std::size_t{3'840});
        QVERIFY(pyramid.levelCount() <= std::size_t{16});
    }

    void deepZoomKeepsTheFinestAvailableBaseBuckets()
    {
        const std::vector<std::vector<PeakBucket>> channels{{
            {-0.1F, 0.1F}, {-0.2F, 0.2F}, {-0.3F, 0.3F}, {-0.4F, 0.4F},
            {-0.5F, 0.5F}, {-0.6F, 0.6F}, {-0.7F, 0.7F}, {-0.8F, 0.8F}}};
        const auto pyramid = PeakPyramid::fromBaseBuckets(channels, 1, 8);

        const auto peaks = pyramid.read(0, 0, 8, 8);
        QCOMPARE(peaks.size(), std::size_t{8});
        QCOMPARE(peaks[1].minimum, -0.2F);
        QCOMPARE(peaks[6].maximum, 0.7F);
    }

    void readWindowReportsTheAlignedSourceRange()
    {
        std::vector<PeakBucket> buckets(10, PeakBucket{-0.5F, 0.5F});
        const auto pyramid = PeakPyramid::fromBaseBuckets(
            {std::move(buckets)}, 10, 100);

        const auto window = pyramid.readWindow(0, 15, 10, 100);
        QCOMPARE(window.start, SampleFrame{10});
        QCOMPARE(window.bucketFrames, SampleFrame{10});
        QCOMPARE(window.buckets.size(), std::size_t{2});
    }

    void invalidChannelAndRangeReturnEmpty()
    {
        const auto pyramid = PeakPyramid::fromBaseBuckets(
            {{{-1.0F, 1.0F}}}, 100, 100);
        QVERIFY(pyramid.read(1, 0, 100, 10).empty());
        QVERIFY(pyramid.read(0, 100, 0, 10).empty());
    }
};

QTEST_APPLESS_MAIN(PeakPyramidTest)

#include "peak_pyramid_test.moc"
