#ifndef PLATINUM_STORE_HPP
#define PLATINUM_STORE_HPP

#include "scene.hpp"

#include <print>
#include <cassert>

#include <loaders/texture.hpp>

namespace pt {

class Store {
public:
  enum class NodeAction {
    None = 0,
    Remove,
    CenterCamera,
  };

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

  Scene::Node createPrimitive(std::string_view name, Mesh&& mesh);

  void update();

  [[nodiscard]] constexpr std::optional<Scene::NodeID> selectedNode() const { return m_selectedNodeId; }
  constexpr void selectNode(std::optional<Scene::NodeID> id) { m_nextNodeId = id; }

  [[nodiscard]] constexpr Scene::RemoveMode& removeMode() { return m_removeMode; }

  constexpr void setNodeAction(NodeAction action, Scene::NodeID id) {
    m_nodeAction = action;
    m_actionNodeId = id;
  }

  constexpr void clearNodeAction() {
    m_nodeAction = NodeAction::None;
    m_actionNodeId = std::nullopt;
  }

  [[nodiscard]] constexpr std::pair<NodeAction, Scene::NodeID> getNodeAction() {
    if (!m_actionNodeId) return {NodeAction::None, m_scene->root().id()};
    return {m_nodeAction, m_actionNodeId.value()};
  }

  constexpr void removeNode(Scene::NodeID id, Scene::RemoveMode mode = Scene::RemoveMode::Recursive) {
    m_removeMode = mode;
    setNodeAction(NodeAction::Remove, id);
  }

  [[nodiscard]] constexpr bool rendering() const { return m_rendering; }
  constexpr void setRendering(bool rendering) { m_rendering = rendering; }

private:
  std::unique_ptr<Scene> m_scene;
  MTL::Device* m_device = nullptr;
  MTL::CommandQueue* m_commandQueue = nullptr;

  std::optional<Scene::NodeID> m_selectedNodeId, m_nextNodeId, m_actionNodeId;
  NodeAction m_nodeAction = NodeAction::None;
  Scene::RemoveMode m_removeMode = Scene::RemoveMode::Recursive;
  bool m_rendering = false;
};

}

#endif //PLATINUM_STORE_HPP
