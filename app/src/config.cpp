#include "config.h"

#include <nlohmann/json.hpp>

#include <fstream>

bool loadConfig(const std::string& path, AppConfig& cfg) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("baidu_api_key")) cfg.baiduApiKey = j["baidu_api_key"].get<std::string>();
        if (j.contains("baidu_secret_key"))
            cfg.baiduSecretKey = j["baidu_secret_key"].get<std::string>();
        if (j.contains("deepseek_api_key"))
            cfg.deepseekApiKey = j["deepseek_api_key"].get<std::string>();
        if (j.contains("llm_model")) cfg.llmModel = j["llm_model"].get<std::string>();
        if (j.contains("llm_base_url")) cfg.llmBaseUrl = j["llm_base_url"].get<std::string>();
    } catch (...) {
        return false;
    }
    return true;
}
