#pragma once

#include <android_native_app_glue.h>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include <unistd.h>

namespace Assets {

struct Font {
    std::vector<char> bytes{};
    int collectionIndex{0};
    std::vector<std::pair<uint32_t, float>> variationSettings{};
    
    Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    Font(Font&&) noexcept = default;
    Font& operator=(Font&&) noexcept = default;
};

class Manager {
  public:
    Manager(android_app* app);
    std::string ensureAvailable(const std::string& asset_name) const;
    std::vector<char> read(const std::string& asset_name) const;
    Font get_font(const std::string& name) const;
  private:
    std::string normalize_path(const std::string& asset_name) const;
    AAssetManager* m_am{nullptr};
    std::string m_data_path{};
};

}