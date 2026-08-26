#include "core/atomic_publish.hpp"

#include <stdexcept>

namespace texas::core {
namespace {

void remove_temporary(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace

void publish_atomic_replace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    const char* failure_message) {
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) return;

    std::error_code exists_error;
    if (!std::filesystem::exists(destination, exists_error) || exists_error) {
        remove_temporary(temporary);
        throw std::runtime_error(failure_message);
    }

    auto backup = destination;
    backup += ".previous";
    if (std::filesystem::exists(backup, exists_error) || exists_error) {
        remove_temporary(temporary);
        throw std::runtime_error(failure_message);
    }

    std::filesystem::rename(destination, backup, error);
    if (error) {
        remove_temporary(temporary);
        throw std::runtime_error(failure_message);
    }

    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code restore_error;
        std::filesystem::rename(backup, destination, restore_error);
        remove_temporary(temporary);
        throw std::runtime_error(failure_message);
    }

    std::error_code cleanup_error;
    std::filesystem::remove(backup, cleanup_error);
}

}  // namespace texas::core
