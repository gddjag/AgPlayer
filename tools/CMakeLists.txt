# Development-only tooling. seek_benchmark measures seek latency through the
# public C ABI with the null backend. fixture_generator is intentionally NOT
# defined here: it is already built from tests/CMakeLists.txt so the test
# custom commands that depend on it remain self-contained.

add_executable(seek_benchmark
    seek_benchmark.cpp
)
target_link_libraries(seek_benchmark PRIVATE agplayer_core)

agplayer_enable_warnings(seek_benchmark)
