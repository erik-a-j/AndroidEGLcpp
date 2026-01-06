#pragma once

#include <android_native_app_glue.h>
#include <string>
#include <vector>

namespace Assets {

class Manager {
  public:
    Manager(android_app* app);
    std::string ensureAvailable(const std::string& asset_name) const;
    std::vector<char> read(const std::string& asset_name) const;
  private:
    std::string normalize_path(const std::string& asset_name) const;
    AAssetManager* m_am{nullptr};
    std::string m_data_path{};
};

}