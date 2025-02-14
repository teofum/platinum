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

/*
 * String literal type to allow strings in template parameters
 * https://ctrpeach.io/posts/cpp20-string-literal-template-parameters/
 */
template<size_t N>
struct StringLiteral {
  constexpr StringLiteral(const char (& str)[N]) noexcept { // NOLINT
    std::copy_n(str, N, value);
  }

  char value[N];
};

}

#endif //PLATINUM_UTILS_HPP
