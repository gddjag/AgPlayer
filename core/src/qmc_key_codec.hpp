#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace agplayer {

// Decode only EKeys embedded in the file. No account, client database or
// network lookup is performed.
bool decode_qmc_embedded_key(std::string_view encoded,
                             std::vector<std::uint8_t>& key);

} // namespace agplayer
