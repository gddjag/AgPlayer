#pragma once

#include <string>

namespace agplayer {

class CoreContext final {
public:
    [[nodiscard]] const std::string& last_error() const noexcept;
    void set_error(std::string value);

private:
    std::string last_error_;
};

} // namespace agplayer
