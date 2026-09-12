#include "visual_spectrum_features.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool value) { if (!value) throw std::runtime_error("feature contract failed"); }
static QJsonObject read(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && doc.isObject());
    return doc.object();
}
int main(int argc, char** argv) {
    try {
        agplayer::VisualSpectrumFeatures analyzer;
        agplayer::VisualSpectrumFeatures::Spectrum bytes{};
        auto f = analyzer.update(bytes, true, false);
        require(f.energy == 0 && std::abs(f.smoothness - .08) < 1e-12);
        analyzer.reset(); bytes.fill(255);
        f = analyzer.update(bytes, true, false);
        require(std::abs(f.energy - .15) < 1e-12 && f.smoothness == 0);
        require(std::abs(f.spectralCentroid - 255.5 * .15) < 1e-12);
        require(f.density == 0);
        f = analyzer.update(bytes, false, true);
        require(std::abs(f.energy - .15 * .86) < 1e-12 && std::abs(f.smoothness - .14) < 1e-12);
        analyzer.reset();
        f = analyzer.update(bytes, false, false);
        require(f.energy == 0 && std::abs(f.smoothness - .08) < 1e-12);
        // Single-bin boundary probes cover every raw band plus the unused tail;
        // legacy bass/mid overlap is intentionally tested separately.
        constexpr std::array<std::size_t, 9> edges{0,2,4,8,19,47,94,187,373};
        for (std::size_t bin = 0; bin < 512; ++bin) {
            analyzer.reset(); bytes.fill(0); bytes[bin] = 255;
            f = analyzer.update(bytes, true, false);
            for (std::size_t band = 0; band < 8; ++band) {
                const auto first = band == 1 ? 0U : (band == 3 ? 8U : edges[band]);
                const auto end = band == 1 ? 8U : (band == 3 ? 47U : edges[band+1]);
                const double expected = bin >= first && bin < end ? .15 / static_cast<double>(end-first) : 0;
                require(std::abs(f.bands[band] - expected) < 1e-12);
            }
            require(std::abs(f.energy - .15 / 512) < 1e-12);
            require(std::abs(f.warmth - (bin < 19 ? .15 : 0)) < 1e-12);
            require(std::abs(f.brightness - (bin >= 47 && bin < 373 ? .15 : 0)) < 1e-12);
            require(std::abs(f.spectralCentroid - .15 * static_cast<double>(bin)) < 1e-12);
        }
        analyzer.reset(); bytes.fill(0); bytes[47] = 255;
        f = analyzer.update(bytes, true, false); require(std::abs(f.sharpness - 1.5) < 1e-12);
        f = analyzer.update(bytes, true, false); require(std::abs(f.sharpness - 1.275) < 1e-12);
        analyzer.update(bytes, false, false);
        f = analyzer.update(bytes, true, false); require(f.sharpness > 1.5);
        if (argc == 1) return 0;
        const QString manifestPath = QString::fromLocal8Bit(argv[1]);
        const auto manifest = read(manifestPath);
        require(manifest.value("sourceUnchanged").toBool());
        const char* names[] = {"subBass", "bass", "lowMid", "mid", "highMid", "presence", "brilliance", "air", "energy", "warmth", "brightness", "sharpness", "smoothness", "density", "spectralCentroid"};
        std::array<double, 15> maxima{};
        int count = 0;
        for (const auto result : manifest.value("results").toArray()) {
            analyzer.reset(); int expectedFrame = 0;
            for (const auto entry : result.toObject().value("frames").toArray()) {
                const auto e = entry.toObject(); require(e.value("frame").toInt(-1) == expectedFrame++);
                const auto path = QDir(QFileInfo(manifestPath).absolutePath()).filePath(QFileInfo(e.value("stateFile").toString()).fileName());
                const auto state = read(path);
                const auto input = state.value("spectrum").toObject().value("bytes").toArray(); require(input.size() == 512);
                for (int i = 0; i < 512; ++i) { const int b = input[i].toInt(-1); require(b >= 0 && b <= 255); bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(b); }
                const auto engine = state.value("engine").toObject();
                f = analyzer.update(bytes, engine.value("isPlaying").toBool(), false);
                const auto wanted = engine.value("features").toObject();
                const std::array<double, 15> actual{f.bands[0],f.bands[1],f.bands[2],f.bands[3],f.bands[4],f.bands[5],f.bands[6],f.bands[7],f.energy,f.warmth,f.brightness,f.sharpness,f.smoothness,f.density,f.spectralCentroid};
                for (std::size_t i = 0; i < actual.size(); ++i) { require(wanted.value(names[i]).isDouble()); maxima[i] = std::max(maxima[i], std::abs(actual[i] - wanted.value(names[i]).toDouble())); }
                ++count;
            }
        }
        require(count > 0);
        for (std::size_t i = 0; i < maxima.size(); ++i) std::cout << names[i] << " max_error=" << maxima[i] << '\n';
        std::cout << "oracle_frames=" << count << '\n';
        for (double error : maxima) require(error < 1e-12);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
