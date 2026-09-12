#pragma once

#include <QDataStream>
#include <QFile>
#include <QString>

#include <cmath>

namespace agplayer {
namespace test {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// Writes a 16-bit 16 kHz WAV file with a sine-burst click every 60/bpm seconds.
// Suitable for verifying BPM analysis accuracy.
inline bool writeClickTrackWav(const QString& path, int bpm,
                               int duration_seconds, int channel_count = 1)
{
    if (bpm <= 0 || duration_seconds <= 0
        || channel_count <= 0 || channel_count > 8) {
        return false;
    }

    constexpr int sample_rate = 16000;
    constexpr int bits_per_sample = 16;
    const int total_samples = sample_rate * duration_seconds;
    const int byte_rate = sample_rate * channel_count * bits_per_sample / 8;
    const int block_align = channel_count * bits_per_sample / 8;
    const int data_size = total_samples * block_align;
    const int file_size = 44 + data_size - 8;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    // RIFF header
    stream.writeRawData("RIFF", 4);
    stream << file_size;
    stream.writeRawData("WAVE", 4);

    // fmt chunk
    stream.writeRawData("fmt ", 4);
    stream << 16;                              // Subchunk1Size
    stream << static_cast<qint16>(1);          // AudioFormat = PCM
    stream << static_cast<qint16>(channel_count);
    stream << sample_rate;
    stream << byte_rate;
    stream << static_cast<qint16>(block_align);
    stream << static_cast<qint16>(bits_per_sample);

    // data chunk
    stream.writeRawData("data", 4);
    stream << data_size;

    const double period_seconds = 60.0 / static_cast<double>(bpm);
    const int period_samples = static_cast<int>(period_seconds * sample_rate);
    if (bpm > 1000 || period_samples <= 0) {
        return false;
    }
    constexpr int click_duration = 400; // samples (~25 ms)
    constexpr double click_freq = 1000.0 * 2.0 * kPi / sample_rate;

    for (int i = 0; i < total_samples; ++i) {
        const int within_period = i % period_samples;
        qint16 sample = 0;
        if (within_period < click_duration) {
            const double envelope = 1.0 - static_cast<double>(within_period) / click_duration;
            sample = static_cast<qint16>(
                std::sin(click_freq * i) * envelope * 32767.0 * 0.9);
        }
        for (int channel = 0; channel < channel_count; ++channel) {
            stream << sample;
        }
    }

    file.flush();
    return file.error() == QFile::NoError;
}

} // namespace test
} // namespace agplayer
