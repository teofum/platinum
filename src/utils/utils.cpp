#include "utils.hpp"

namespace pt::utils {

std::optional<fs::path> fileOpen(const fs::path &defaultPath,
                                 const std::string &filters) {
  char *path = nullptr;
  auto result = NFD_OpenDialog(filters.c_str(), defaultPath.c_str(), &path);

  if (result == NFD_OKAY) {
    fs::path filePath(path);
    free(path);
    return filePath;
  } else {
    return std::nullopt;
  }
}

std::optional<fs::path> fileSave(const fs::path &defaultPath,
                                 const std::string &filters) {
  char *path = nullptr;
  auto result = NFD_SaveDialog(filters.c_str(), defaultPath.c_str(), &path);

  if (result == NFD_OKAY) {
    fs::path filePath(path);
    free(path);
    return filePath;
  } else {
    return std::nullopt;
  }
}

} // namespace pt::utils
