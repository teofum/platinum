#include "store.hpp"

namespace pt {

Store::Store() noexcept: m_scene() {
}

Store::~Store() {
  m_device->release();
}

}