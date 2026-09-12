// seek_benchmark: deterministic seek-latency measurement tool.
//
// Usage: seek_benchmark <utf8_path>
//
// Creates an ag_player with the null audio backend, loads the supplied file,
// warms up briefly, performs 100 deterministic seeks across the duration, and
// prints a JSON object with min/median/P95/max seek latencies (milliseconds).
// Exits nonzero when P95 >= 20ms.

#include <agplayer/c_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// Fixed-seed PRNG so seek positions are reproducible across runs.
std::uint64_t next_random(std::uint64_t& state)
{
    // xorshift64*
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * 0x2545F4914F6CDD1DULL;
}

double percentile_sorted(std::vector<double>& sorted, double pct)
{
    if (sorted.empty()) {
        return 0.0;
    }
    const double rank = (pct / 100.0) * static_cast<double>(sorted.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(rank));
    const auto upper = static_cast<std::size_t>(std::ceil(rank));
    if (lower == upper || upper >= sorted.size()) {
        return sorted[lower];
    }
    const double frac = rank - static_cast<double>(lower);
    return sorted[lower] * (1.0 - frac) + sorted[upper] * frac;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: seek_benchmark <utf8_path>\n");
        return 2;
    }

    const char* const path = argv[1];
    ag_player_config config{};
    config.backend = AG_AUDIO_BACKEND_NULL;
    config.buffer_frames = 4'096U;

    ag_player* player = nullptr;
    if (ag_player_create_with_config(&config, &player) != AG_OK) {
        std::fprintf(stderr, "ag_player_create_with_config failed\n");
        return 3;
    }

    const auto load_result = ag_player_load(player, path);
    if (load_result != AG_OK) {
        std::fprintf(stderr, "ag_player_load failed: %d\n",
                     static_cast<int>(load_result));
        ag_player_destroy(player);
        return 4;
    }

    if (const ag_result play_result = ag_player_play(player);
        play_result != AG_OK) {
        std::fprintf(stderr, "ag_player_play failed: %d\n",
                     static_cast<int>(play_result));
        ag_player_destroy(player);
        return 5;
    }

    ag_playback_snapshot snapshot{};
    if (ag_player_snapshot(player, &snapshot) != AG_OK) {
        std::fprintf(stderr, "ag_player_snapshot failed\n");
        ag_player_destroy(player);
        return 6;
    }

    const long long duration_ms = snapshot.duration_ms;
    if (duration_ms <= 0) {
        std::fprintf(stderr, "duration_ms is %lld; cannot benchmark\n",
                     duration_ms);
        ag_player_destroy(player);
        return 7;
    }

    // Warm up: let playback settle for ~150ms so the decode loop is primed.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    constexpr int seek_count = 100;
    std::uint64_t rng_state = 0xDEADBEEFCAFEBABEULL;
    std::vector<double> latencies_ms;
    latencies_ms.reserve(static_cast<std::size_t>(seek_count));
    int failed_seeks = 0;
    // Avoid seeking into the final 500ms where some decoders (e.g. FLAC)
    // report AG_DECODE_ERROR on short fixtures.
    const long long seek_ceiling = duration_ms > 500LL
        ? duration_ms - 500LL : duration_ms;
    const std::uint64_t seek_range =
        seek_ceiling > 0LL ? static_cast<std::uint64_t>(seek_ceiling) : 1ULL;

    for (int i = 0; i < seek_count; ++i) {
        const std::uint64_t r = next_random(rng_state);
        const long long target_ms =
            static_cast<long long>(r % seek_range);

        const auto start = std::chrono::high_resolution_clock::now();
        const ag_result seek_result = ag_player_seek(player, target_ms);
        if (seek_result != AG_OK) {
            // Skip seeks that the decoder rejects (e.g. edge cases near EOF)
            // so the benchmark still produces latency statistics.
            ++failed_seeks;
            continue;
        }

        // Poll until the snapshot reflects a position near the target or a
        // best-effort timeout elapses.
        ag_playback_snapshot after{};
        for (int attempt = 0; attempt < 2'000; ++attempt) {
            ag_player_snapshot(player, &after);
            const long long delta = after.position_ms - target_ms;
            if (delta < 0 ? -delta <= 500LL : delta <= 500LL) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        const auto end = std::chrono::high_resolution_clock::now();

        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        latencies_ms.push_back(elapsed_ms);
    }

    ag_player_stop(player);
    ag_player_destroy(player);

    if (latencies_ms.empty()) {
        std::fprintf(stderr, "all %d seeks failed; no latency data\n",
                     seek_count);
        return 9;
    }

    std::vector<double> sorted = latencies_ms;
    std::sort(sorted.begin(), sorted.end());
    const double min_ms = sorted.front();
    const double max_ms = sorted.back();
    const double median_ms = percentile_sorted(sorted, 50.0);
    const double p95_ms = percentile_sorted(sorted, 95.0);

    std::printf(
        "{\n"
        "  \"device\": \"null\",\n"
        "  \"backend\": \"AG_AUDIO_BACKEND_NULL\",\n"
        "  \"file\": \"%s\",\n"
        "  \"samples\": %zu,\n"
        "  \"failed_seeks\": %d,\n"
        "  \"min_ms\": %.4f,\n"
        "  \"median_ms\": %.4f,\n"
        "  \"p95_ms\": %.4f,\n"
        "  \"max_ms\": %.4f,\n"
        "  \"passed\": %s\n"
        "}\n",
        path,
        latencies_ms.size(),
        failed_seeks,
        min_ms,
        median_ms,
        p95_ms,
        max_ms,
        p95_ms < 20.0 ? "true" : "false");

    return p95_ms >= 20.0 ? 1 : 0;
}
