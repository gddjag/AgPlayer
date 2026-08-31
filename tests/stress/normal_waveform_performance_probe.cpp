// A deliberately narrow comparator for the frequency-colour waveform QA gate.
// It measures only the established public ag_waveform_analyze() path.  Keeping
// this executable separate from functional tests prevents new frequency test
// cases from being mistaken for a normal-waveform performance regression.
#undef NDEBUG

#include <agplayer/c_api.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

struct MemorySnapshot final {
    std::uint64_t resident_bytes = 0;
    std::uint64_t peak_resident_bytes = 0;
};

MemorySnapshot process_memory() noexcept
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
        != FALSE) {
        return {static_cast<std::uint64_t>(counters.WorkingSetSize),
                static_cast<std::uint64_t>(counters.PeakWorkingSetSize)};
    }
#endif
    return {};
}

const char* acquire_measurement_priority() noexcept
{
#ifdef _WIN32
    if (SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) == FALSE) {
        return "Unavailable";
    }
    return GetPriorityClass(GetCurrentProcess()) == HIGH_PRIORITY_CLASS
        ? "High" : "Unavailable";
#else
    return "Unsupported";
#endif
}

std::uint64_t acquire_measurement_affinity(const std::uint64_t requested_mask) noexcept
{
#ifdef _WIN32
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)
        == FALSE
        || process_mask == 0) {
        return 0U;
    }
    const std::uint64_t available_mask =
        static_cast<std::uint64_t>(process_mask);
    const std::uint64_t selected_mask = requested_mask == 0U
        ? available_mask & (~available_mask + 1U)
        : requested_mask;
    if (selected_mask == 0U || (selected_mask & ~available_mask) != 0U
        || SetProcessAffinityMask(GetCurrentProcess(),
                                  static_cast<DWORD_PTR>(selected_mask))
               == FALSE) {
        return 0U;
    }
    process_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)
            == FALSE
        || static_cast<std::uint64_t>(process_mask) != selected_mask) {
        return 0U;
    }
    return selected_mask;
#else
    (void)requested_mask;
    return 0U;
#endif
}

void analyze_normal_waveform(const char* source_path)
{
    ag_waveform* waveform = nullptr;
    const ag_result status = ag_waveform_analyze(source_path, 512U, nullptr,
                                                  nullptr, nullptr, &waveform);
    assert(status == AG_OK);
    assert(waveform != nullptr);
    assert(ag_waveform_count(waveform) == 512U);
    ag_waveform_destroy(waveform);
}

struct Measurement final {
    std::vector<double> samples_milliseconds;
    double median_milliseconds = 0.0;
    double elapsed_milliseconds = 0.0;
};

Measurement measure_normal_waveform(const char* source_path,
                                    const unsigned int iterations)
{
    Measurement measurement;
    measurement.samples_milliseconds.reserve(iterations);
    const auto all_started = std::chrono::steady_clock::now();
    for (unsigned int iteration = 0; iteration < iterations; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        analyze_normal_waveform(source_path);
        const auto finished = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::milli> elapsed =
            finished - started;
        measurement.samples_milliseconds.push_back(elapsed.count());
    }
    const auto all_finished = std::chrono::steady_clock::now();
    std::vector<double> sorted = measurement.samples_milliseconds;
    const std::size_t middle = sorted.size() / 2U;
    std::nth_element(sorted.begin(), sorted.begin() + middle, sorted.end());
    measurement.median_milliseconds = sorted[middle];
    measurement.elapsed_milliseconds =
        std::chrono::duration<double, std::milli>(all_finished - all_started)
            .count();
    return measurement;
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 3 || argc == 5 || argc == 7);
    const char* const source_path = argv[1];
    const bool hot = std::strcmp(argv[2], "--hot") == 0;
    assert(hot || std::strcmp(argv[2], "--cold") == 0);
    unsigned int iterations = 100U;
    std::uint64_t requested_affinity_mask = 0U;
    if (argc >= 5) {
        assert(std::strcmp(argv[3], "--iterations") == 0);
        const unsigned long parsed = std::strtoul(argv[4], nullptr, 10);
        assert(parsed >= 30UL && parsed <= 10'000UL);
        iterations = static_cast<unsigned int>(parsed);
    }
    if (argc == 7) {
        assert(std::strcmp(argv[5], "--affinity-mask") == 0);
        const unsigned long long parsed = std::strtoull(argv[6], nullptr, 0);
        assert(parsed != 0ULL);
        requested_affinity_mask = static_cast<std::uint64_t>(parsed);
    }
    const char* const priority_class = acquire_measurement_priority();
    const std::uint64_t affinity_mask =
        acquire_measurement_affinity(requested_affinity_mask);
    if (std::strcmp(priority_class, "High") != 0 || affinity_mask == 0U) {
        return EXIT_FAILURE;
    }

    if (hot) {
        analyze_normal_waveform(source_path);
    }

    const Measurement measurement =
        measure_normal_waveform(source_path, iterations);
    const MemorySnapshot memory = process_memory();

    std::cout << std::fixed << std::setprecision(3)
              << "{\"mode\":\"" << (hot ? "hot" : "cold")
              << "\",\"analysisIterations\":" << iterations
              << ",\"priorityClass\":\"" << priority_class << "\""
              << ",\"affinityMask\":" << affinity_mask
              << ",\"warmupIterations\":" << (hot ? 1U : 0U)
              << ",\"analysisMilliseconds\":"
              << measurement.median_milliseconds
              << ",\"analysisElapsedMilliseconds\":"
              << measurement.elapsed_milliseconds
              << ",\"analysisSamplesMilliseconds\":[";
    for (std::size_t index = 0U;
         index < measurement.samples_milliseconds.size(); ++index) {
        if (index != 0U) std::cout << ',';
        std::cout << measurement.samples_milliseconds[index];
    }
    std::cout << ']'
              << ",\"residentBytes\":" << memory.resident_bytes
              << ",\"peakResidentBytes\":" << memory.peak_resident_bytes
              << "}" << std::endl;
    return 0;
}
