#pragma once
#include "config.h"

#include <string>

struct LlmResult {
    bool ok = false;
    std::string content;
    std::string detail;
};

LlmResult organizeNotes(const AppConfig& cfg, const std::string& ocrText,
                        int maxTokens = 2000);
