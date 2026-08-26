#pragma once

#include <filesystem>

namespace texas::core {

void publish_atomic_replace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    const char* failure_message);

}  // namespace texas::core
