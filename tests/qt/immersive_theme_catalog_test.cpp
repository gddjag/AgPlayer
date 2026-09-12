#include "immersive_theme_catalog.hpp"

#include <QTest>

#include <array>
#include <cmath>

using namespace agplayer::immersive;

class ImmersiveThemeCatalogTest final : public QObject {
    Q_OBJECT

private slots:
    void violetHeartHasDarkPurpleBodyAndPinkBeatLight()
    {
        const auto* theme = findBuiltInTheme("violet-heart");
        QVERIFY(theme);
        const std::array<SrgbRgb, 8> expected{{{5,2,10},{107,57,155},{4,2,8},
            {201,122,255},{165,90,218},{255,164,218},{255,246,159},{201,122,255}}};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto color = workingLinearToSrgb(toWorkingLinear(theme->colors[i]));
            QCOMPARE(int(std::round(color.red * 255)), int(expected[i].red));
            QCOMPARE(int(std::round(color.green * 255)), int(expected[i].green));
            QCOMPARE(int(std::round(color.blue * 255)), int(expected[i].blue));
        }
        QCOMPARE(theme->glowIntensity, 1.10F);
    }
    void hasStableOrderAndMinimalMonochromeDefault();
    void lookupAndReferenceNumericColorsAreExact();
    void colorEncodingBoundaryProducesWorkingLinearRgb();
    void hexBackedThemesConvertFromTheirOriginalBytes();
};

void ImmersiveThemeCatalogTest::hasStableOrderAndMinimalMonochromeDefault()
{
    const auto& themes = builtInThemes();
    QCOMPARE(static_cast<int>(themes.size()), 13);
    QVERIFY(themes.front().id == "ink-wash");
    QVERIFY(themes.at(4).id == "minimal-monochrome");
    QVERIFY(themes.back().id == "violet-heart");
    QVERIFY(findBuiltInTheme("daybreak-lime") == nullptr);
    QVERIFY(defaultBuiltInTheme().id == "minimal-monochrome");
}

void ImmersiveThemeCatalogTest::lookupAndReferenceNumericColorsAreExact()
{
    const auto* nocturnal = findBuiltInTheme("nocturnal");
    QVERIFY(nocturnal != nullptr);
    QVERIFY(nocturnal->displayName == "Nocturnal");
    QCOMPARE(nocturnal->glowIntensity, 1.0F);

    const auto coolCore = nocturnal->colors.at(themeColorIndex(ThemeColorRole::CoolCore));
    QVERIFY(coolCore.encoding == ThemeColorEncoding::WorkingLinear);
    QCOMPARE(coolCore.red, 0.0F);
    QCOMPARE(coolCore.green, 0.3F);
    QCOMPARE(coolCore.blue, 1.0F);

    const auto ripple = nocturnal->colors.at(themeColorIndex(ThemeColorRole::Ripple));
    QCOMPARE(ripple.red, 0.2F);
    QCOMPARE(ripple.green, 0.9F);
    QCOMPARE(ripple.blue, 1.0F);
    QVERIFY(findBuiltInTheme("not-a-theme") == nullptr);
}

void ImmersiveThemeCatalogTest::colorEncodingBoundaryProducesWorkingLinearRgb()
{
    const auto* glacier = findBuiltInTheme("glacier-day");
    QVERIFY(glacier != nullptr);
    const auto background = glacier->colors.at(themeColorIndex(ThemeColorRole::BasePrimary));
    QVERIFY(background.encoding == ThemeColorEncoding::WorkingLinear);
    QVERIFY(std::abs(background.red - srgbChannelToLinear(216.0F / 255.0F)) < 1.0e-6F);
    QVERIFY(std::abs(background.green - srgbChannelToLinear(230.0F / 255.0F)) < 1.0e-6F);
    QVERIFY(std::abs(background.blue - srgbChannelToLinear(234.0F / 255.0F)) < 1.0e-6F);

    const ThemeColorInput encodedSrgb{ThemeColorEncoding::Srgb,
                                      216.0F / 255.0F,
                                      230.0F / 255.0F,
                                      234.0F / 255.0F};
    const LinearRgb resolved = toWorkingLinear(encodedSrgb);
    QVERIFY(std::abs(resolved.red - srgbChannelToLinear(216.0F / 255.0F)) < 1.0e-6F);
    QVERIFY(std::abs(resolved.green - srgbChannelToLinear(230.0F / 255.0F)) < 1.0e-6F);
    QVERIFY(std::abs(resolved.blue - srgbChannelToLinear(234.0F / 255.0F)) < 1.0e-6F);

    const auto encoded = workingLinearToSrgb({0.5F, 0.25F, 1.0F});
    QVERIFY(std::abs(encoded.red - 0.735357F) < 1.0e-5F);
    QVERIFY(std::abs(encoded.green - 0.537099F) < 1.0e-5F);
    QCOMPARE(encoded.blue, 1.0F);
}

void ImmersiveThemeCatalogTest::hexBackedThemesConvertFromTheirOriginalBytes()
{
    struct Expected { const char* id; int red; int green; int blue; };
    const std::array expected = {
        Expected{"glacier-day", 216, 230, 234}, Expected{"koi-pond", 18, 58, 54},
        Expected{"coral-reef", 64, 37, 42}, Expected{"moss-glass", 46, 58, 36},
        Expected{"blue-hour", 39, 60, 85}, Expected{"porcelain-teal", 221, 232, 228},
        Expected{"wine-signal", 58, 36, 48}, Expected{"violet-heart", 5, 2, 10},
    };
    for (const Expected& expectedTheme : expected) {
        const auto* theme = findBuiltInTheme(expectedTheme.id);
        QVERIFY(theme != nullptr);
        const auto source = theme->colors.at(themeColorIndex(ThemeColorRole::BasePrimary));
        QVERIFY(source.encoding == ThemeColorEncoding::WorkingLinear);
        const auto encoded = workingLinearToSrgb(toWorkingLinear(source));
        QCOMPARE(static_cast<int>(std::round(encoded.red * 255.0F)), expectedTheme.red);
        QCOMPARE(static_cast<int>(std::round(encoded.green * 255.0F)), expectedTheme.green);
        QCOMPARE(static_cast<int>(std::round(encoded.blue * 255.0F)), expectedTheme.blue);
    }
}

QTEST_APPLESS_MAIN(ImmersiveThemeCatalogTest)

#include "immersive_theme_catalog_test.moc"
