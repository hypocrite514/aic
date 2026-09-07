#pragma once
#include <string>

struct AppConfig {
    std::string baiduApiKey;
    std::string baiduSecretKey;
    std::string deepseekApiKey;
    std::string llmModel = "deepseek-chat";
    std::string llmBaseUrl = "https://api.deepseek.com";

    bool baiduReady() const { return !baiduApiKey.empty() && !baiduSecretKey.empty(); }
    bool llmReady() const { return !deepseekApiKey.empty(); }
};

bool loadConfig(const std::string& path, AppConfig& cfg);
