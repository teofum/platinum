#ifndef PLATINUM_STORE_HPP
#define PLATINUM_STORE_HPP

#include "scene.hpp"

#include <print>
#include <cassert>

#include <loaders/texture.hpp>

namespace pt {

class Store {
public:
  explicit Store() noexcept;

  ~Store();

  [[nodiscard]] constexpr auto& scene() {
    return *m_scene;
  }

  [[nodiscard]] constexpr auto device() {
    if (m_device == nullptr) {
      std::println(stderr, "Store: Attempted to get device before init!");
      assert(false);
    }

    return m_device;
  }

  constexpr void setDevice(MTL::Device* device) {
    m_device = device->retain();
  }

  constexpr void setCommandQueue(MTL::CommandQueue* commandQueue) {
    m_commandQueue = commandQueue->retain();
  }

  void open();
  void saveAs();

  void importGltf();
  void importTexture(loaders::texture::TextureType type);

private:
  std::unique_ptr<Scene> m_scene;
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;
};

}

#endif //PLATINUM_STORE_HPP
