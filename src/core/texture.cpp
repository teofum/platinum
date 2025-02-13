#include "texture.hpp"

namespace pt {

Texture::Texture(MTL::Texture* texture, std::string_view name, bool alpha) noexcept
	: m_texture(texture), m_name(name), m_alpha(alpha) {}

Texture::Texture(Texture&& t) noexcept {
  m_texture = t.m_texture;
  m_name = std::move(t.m_name);
  m_alpha = t.m_alpha;
  
  t.m_texture = nullptr;
}

Texture& Texture::operator=(Texture&& t) noexcept {
  m_texture = t.m_texture;
  m_name = std::move(t.m_name);
  m_alpha = t.m_alpha;
  
  t.m_texture = nullptr;
  
  return *this;
}

Texture::~Texture() {
  m_texture->release();
}

}
