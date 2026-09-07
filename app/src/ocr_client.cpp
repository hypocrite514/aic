#include "ocr_client.h"
#include "net_http.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <mutex>

namespace {

std::mutex g_tokenMutex;
std::string g_cachedToken;
long long g_tokenExpiresAt = 0;

}  // namespace

bool fetchBaiduToken(const AppConfig& cfg, std::string& token, std::string& error) {
    std::string url =
        "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials"
        "&client_id=" + urlEncode(cfg.baiduApiKey) +
        "&client_secret=" + urlEncode(cfg.baiduSecretKey);

    HttpResponse resp = httpsRequest("POST", url, "", "application/x-www-form-urlencoded");
    if (!resp.ok()) {
        error = resp.error.empty() ? "token http " + std::to_string(resp.status) : resp.error;
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(resp.body);
        if (!j.contains("access_token")) {
            error = "no access_token in response";
            return false;
        }
        token = j["access_token"].get<std::string>();
        long long expires = 0;
        if (j.contains("expires_in")) expires = j["expires_in"].get<long long>();
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        g_cachedToken = token;
        g_tokenExpiresAt = static_cast<long long>(time(nullptr)) + expires - 3600;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

OcrResult baiduOcr(const AppConfig& cfg, const std::vector<unsigned char>& jpgBytes,
                   const std::string& token, bool handwriting) {
    OcrResult out;
    std::string endpoint = handwriting ? "handwriting" : "general_basic";
    std::string url = "https://aip.baidubce.com/rest/2.0/ocr/v1/" + endpoint;
    std::string form = "access_token=" + urlEncode(token) +
                       "&image=" + urlEncode(base64Encode(jpgBytes));

    HttpResponse resp = httpsRequest("POST", url, form, "application/x-www-form-urlencoded");
    if (!resp.ok()) {
        out.detail = resp.error.empty() ? "http " + std::to_string(resp.status) : resp.error;
        out.status = "error";
        return out;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(resp.body);
        if (j.contains("error_code")) {
            out.detail = "baidu error_code " + std::to_string(j["error_code"].get<int>());
            out.status = "error";
            return out;
        }
        std::string text;
        if (j.contains("words_result") && j["words_result"].is_array()) {
            for (const auto& w : j["words_result"]) {
                if (w.contains("words")) {
                    if (!text.empty()) text += "\n";
                    text += w["words"].get<std::string>();
                }
            }
        }
        out.ok = !text.empty();
        out.status = "ok";
        out.text = text;
        if (text.empty()) out.detail = "no words recognized";
        return out;
    } catch (const std::exception& e) {
        out.detail = e.what();
        out.status = "error";
        return out;
    }
}
