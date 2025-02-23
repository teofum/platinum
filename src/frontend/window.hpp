#ifndef PLATINUM_WINDOW_HPP
#define PLATINUM_WINDOW_HPP

#include <core/store.hpp>

namespace pt::frontend {

class Window {
public:
  explicit constexpr Window(Store& store, bool* open = nullptr) noexcept
    : m_store(store), m_open(open) {}

  virtual void render() = 0;

protected:
  Store& m_store;
  bool* m_open;
};

}

#endif //PLATINUM_WINDOW_HPP
