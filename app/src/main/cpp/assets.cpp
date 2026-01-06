#include "assets.hpp"

#define TAG_NAMESPACE "Assets"
#include "logging.hpp"

#include <fstream>
#include <cerrno>
#include <cstddef>

namespace Assets {

Manager::Manager(android_app* app) {
    if (app && app->activity) {
        m_am = app->activity->assetManager;
        if (app->activity->internalDataPath) {
            m_data_path = app->activity->internalDataPath;
        }
        logx::If("internalDataPath={}", !m_data_path.empty() ? m_data_path : "(null)");
    } else {
        logx::E("construction failed");
    }
}

std::string Manager::normalize_path(const std::string& asset_name) const {
    if (m_data_path.empty() || !m_am) return {};

    auto const base_beg = asset_name.find_last_of('/');
    const char* base = asset_name.c_str() + ((base_beg != std::string::npos) ? base_beg + 1 : 0);
    std::string out = m_data_path + "/" + base;
    return out;
}

std::string Manager::ensureAvailable(const std::string& asset_name) const {
    std::string out = normalize_path(asset_name);
    if (out.empty()) return {};
    
    logx::If("ensureAvailable: target path={}", out);
    {
        std::ifstream ftest(out, std::ios::binary);
        if (ftest.is_open()) { return out; }
    }

    logx::If("ensureAvailable: opening {}", asset_name);
    AAsset* a = AAssetManager_open(m_am, asset_name.c_str(), AASSET_MODE_STREAMING);
    if (!a) {
        logx::Ef("ensureAvailable: not found: {}", asset_name);
        return {};
    }

    std::ofstream f(out, std::ios::binary);
    if (!f.is_open()) {
        logx::Ef("ensureAvailable: fopen(wb) failed: {} (errno={})", out, errno);
        AAsset_close(a);
        return {};
    }

    char buf[16 * 1024];
    int r;
    size_t total = 0;
    while ((r = AAsset_read(a, buf, (int)sizeof(buf))) > 0) {
        size_t rsize = static_cast<size_t>(r);
        f.write(buf, rsize);
        if (!f) {
            logx::Ef("ensureAvailable: write failed (bad={} fail={})", (int)f.bad(), (int)f.fail());
            f.close();
            AAsset_close(a);
            return {};
        }
        total += rsize;
    }
    if (r < 0) {
        logx::E("ensureAvailable: AAsset_read failed");
        f.close();
        AAsset_close(a);
        return {};
    }

    f.close();
    AAsset_close(a);
    logx::If("ensureAvailable: copied {} -> {} ({} bytes)", asset_name, out, total);
    return out;
}
std::vector<char> Manager::read(const std::string& asset_name) const {
    std::string path = ensureAvailable(asset_name);
    if (path.empty()) return {};
    
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    
    size_t fsize = f.tellg();
    if (fsize == 0) return {};
    
    f.seekg(0);
    std::vector<char> out(fsize + 1);
    f.read(out.data(), fsize);
    if (!f) return {};
    
    out[fsize] = '\0';
    return out;
}

}