#ifndef PLATINUM_UTILS_HPP
#define PLATINUM_UTILS_HPP

#include <cstddef>

namespace pt::utils {

constexpr size_t align(size_t n, size_t to) {
  return ((n - 1) / to + 1) * to;
}

}

#endif //PLATINUM_UTILS_HPP
