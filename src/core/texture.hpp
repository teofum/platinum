#ifndef PLATINUM_TEXTURE_HPP
#define PLATINUM_TEXTURE_HPP

#include <Metal/Metal.hpp>

namespace pt {

class Texture {
public:
  Texture(MTL::Texture* texture, std::string_view name, bool alpha) noexcept;

  Texture(const Texture& m) noexcept = delete;
  Texture(Texture&& m) noexcept;

  Texture& operator=(const Texture& m) = delete;
  Texture& operator=(Texture&& m) noexcept;
  
  ~Texture();
  
  [[nodiscard]] constexpr MTL::Texture* texture() { return m_texture; }
  [[nodiscard]] constexpr std::string_view name() { return m_name; }
  [[nodiscard]] constexpr bool hasAlpha() const { return m_alpha; }
  
private:
  MTL::Texture* m_texture;
  std::string m_name;
  bool m_alpha;
};

}

#endif //PLATINUM_TEXTURE_HPP
