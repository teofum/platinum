#ifndef PLATINUM_LOADER_EXR_HPP
#define PLATINUM_LOADER_EXR_HPP

#include <expected>
#include <string>
#include <string_view>

#include <tinyexr.h>

namespace pt::loaders::exr {

std::expected<EXRImage, std::string> load(std::string_view path);

} // namespace pt::loaders::exr

#endif // PLATINUM_LOADER_EXR_HPP
