#include "core_context.hpp"

#include <utility>

namespace agplayer {

const std::string& CoreContext::last_error() const noexcept
{
    return last_error_;
}

void CoreContext::set_error(std::string value)
{
    last_error_ = std::move(value);
}

} // namespace agplayer
