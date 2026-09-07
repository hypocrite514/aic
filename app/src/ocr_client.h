#pragma once
#include "config.h"

#include <string>
#include <vector>

struct OcrResult {
    bool ok = false;
    std::string status;
    std::string text;
    std::string detail;
};

bool fetchBaiduToken(const AppConfig& cfg, std::string& token, std::string& error);

OcrResult baiduOcr(const AppConfig& cfg, const std::vector<unsigned char>& jpgBytes,
                   const std::string& token, bool handwriting);
