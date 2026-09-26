# Lossless identification UI sources are kept together so the top-level
# integration only needs one include after agplayer_qt has been created.
target_sources(agplayer_qt PRIVATE
    "${CMAKE_SOURCE_DIR}/qt/src/lossless_evidence_item.cpp"
    "${CMAKE_SOURCE_DIR}/qt/src/lossless_evidence_item.hpp"
)
