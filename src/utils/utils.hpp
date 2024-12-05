#ifndef PLATINUM_UTILS_HPP
#define PLATINUM_UTILS_HPP

#include <filesystem>
#include <cstddef>
#include <nfd.h>

namespace fs = std::filesystem;

namespace pt::utils {

constexpr size_t align(size_t n, size_t to) {
  return ((n - 1) / to + 1) * to;
}

std::optional<fs::path> fileOpen(const fs::path& defaultPath, const std::string& filters = "");

std::optional<fs::path> fileSave(const fs::path& defaultPath, const std::string& filters = "");

}

#endif //PLATINUM_UTILS_HPP
