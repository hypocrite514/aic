#include "llm_client.h"
#include "net_http.h"

#include <nlohmann/json.hpp>

LlmResult organizeNotes(const AppConfig& cfg, const std::string& ocrText,
                        int maxTokens) {
    LlmResult out;

    nlohmann::json body;
    body["model"] = cfg.llmModel;
    body["temperature"] = 0.3;
    body["max_tokens"] = maxTokens;
    body["messages"] = nlohmann::json::array();
    body["messages"].push_back({
        {"role", "system"},
        {"content",
         "你是一名严谨的课堂笔记整理助手。用户提供黑板/白板照片识别出的原始文本（可能含识别噪声、"
         "顺序混乱、专业术语误识）。请将其整理为结构化 Markdown 笔记，要求：\n"
         "1. 提取/补全标题层级（用 # 或 ##），按知识点归纳编号要点，删除明显识别噪声；\n"
         "2. 忠于原文：不得虚构或编造原文没有的知识点、公式或结论；\n"
         "3. 专业术语、数学公式尽量保持原文原样，行内公式用 $...$，独立公式用 $$...$$；\n"
         "4. 明显识别不清、无法判断的内容用 [原文不清] 标注，不要猜测；\n"
         "5. 若你在整理中做了补充说明或延伸解释，统一放在文末「补充理解」小节，并明确它与原文区分；\n"
         "6. 末尾用一个「重点提示」小节列出最关键的 3-5 条；\n"
         "7. 整体输出务必精炼，正文不超过 600 字；\n"
         "8. 只输出 Markdown 内容本身，不要任何解释性文字。"}
    });
    body["messages"].push_back({{"role", "user"},
                                {"content", "以下是识别文本：\n\n" + ocrText}});

    std::string url = cfg.llmBaseUrl + "/chat/completions";
    HttpResponse resp = httpsRequest(
        "POST", url, body.dump(), "application/json",
        {{"Authorization", "Bearer " + cfg.deepseekApiKey}});

    if (!resp.ok()) {
        out.detail = resp.error.empty() ? "http " + std::to_string(resp.status) : resp.error;
        return out;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(resp.body);
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
            out.detail = "no choices in response";
            return out;
        }
        out.content = j["choices"][0]["message"]["content"].get<std::string>();
        out.ok = !out.content.empty();
        return out;
    } catch (const std::exception& e) {
        out.detail = e.what();
        return out;
    }
}
