#include "util.h"
#include <shlobj.h>
#include <fstream>

namespace om {

std::string appDataDir() {
  wchar_t path[MAX_PATH] = {0};
  std::wstring dir;
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) dir = path;
  else dir = L".";
  dir += L"\\OrangeM";
  CreateDirectoryW(dir.c_str(), nullptr);
  return w2u(dir);
}

bool readFileBytes(const std::string& path, std::string& out) {
  std::ifstream f(path.c_str(), std::ios::binary);
  if (!f) return false;
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  out = data;
  return true;
}

bool writeFileBytes(const std::string& path, const std::string& data) {
  std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
}

} // namespace om
