#include "exr.hpp"

namespace pt::loaders::exr {

std::expected<EXRImage, std::string> load(std::string_view path) {
  // Read EXR version
  EXRVersion version;
  if (ParseEXRVersionFromFile(&version, path.data()) != 0) {
    return std::unexpected("Invalid EXR file.");
  }

  if (version.multipart) {
    return std::unexpected("Multipart EXR not supported.");
  }

  // Read EXR header
  EXRHeader header;
  const char *err = NULL;
  if (ParseEXRHeaderFromFile(&header, &version, path.data(), &err) != 0) {
    auto error = std::string("EXR parse error: ");
    error += err;

    FreeEXRErrorMessage(err);
    return std::unexpected(error);
  }

  EXRImage image;
  InitEXRImage(&image);

  if (LoadEXRImageFromFile(&image, &header, path.data(), &err)) {
    auto error = std::string("EXR load error: ");
    error += err;

    FreeEXRErrorMessage(err);
    FreeEXRHeader(&header);
    return std::unexpected(error);
  }

  FreeEXRHeader(&header);
  return image;
}

} // namespace pt::loaders::exr
