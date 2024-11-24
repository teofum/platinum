#ifndef PLATINUM_STORE_HPP
#define PLATINUM_STORE_HPP

#include "scene.hpp"

namespace pt {

class Store {
public:
  explicit Store() noexcept;

  [[nodiscard]] constexpr auto& scene() {
    return m_scene;
  }

private:
  Scene m_scene;
};

}

#endif //PLATINUM_STORE_HPP
