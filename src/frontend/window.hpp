#ifndef PLATINUM_WINDOW_HPP
#define PLATINUM_WINDOW_HPP

#include <core/store.hpp>
#include "state.hpp"

namespace pt::frontend {

class Window {
public:
  constexpr Window(Store& store, State& state, bool* open = nullptr) noexcept
    : m_store(store), m_state(state), m_open(open) {
  }

  virtual void render() = 0;

protected:
  Store& m_store;
  State& m_state;

  bool* m_open;
};

}

#endif //PLATINUM_WINDOW_HPP
