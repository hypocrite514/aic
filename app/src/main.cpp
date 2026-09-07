#include "httplib.h"
#include "config.h"
#include "llm_client.h"
#include "ocr_client.h"
#include "pipeline.h"
#include <nlohmann/json.hpp>

#include <opencv2/opencv.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

const int kPort = 8080;
const size_t kMaxFileBytes = 25 * 1024 * 1024;
const int kMaxFiles = 30;
const size_t kMaxCachedResults = 50;

std::mutex g_resultMutex;
std::map<std::string, std::vector<unsigned char>> g_results;
std::queue<std::string> g_resultOrder;
std::atomic<unsigned long long> g_counter{0};

std::string randomHexId() {
    static std::mt19937_64 rng(
        static_cast<unsigned long long>(std::chrono::high_resolution_clock::now()
                                            .time_since_epoch()
                                            .count()));
    unsigned long long v = rng();
    char buf[40];
    snprintf(buf, sizeof(buf), "%016llx%016llx", v,
             static_cast<unsigned long long>(++g_counter));
    return std::string(buf);
}

std::string exeDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring wide(path);
    size_t pos = wide.find_last_of(L"\\/");
    if (pos != std::wstring::npos) wide = wide.substr(0, pos);
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr,
                                  nullptr);
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr,
                        nullptr);
    return out;
#else
    return ".";
#endif
}

std::string storeJpeg(const cv::Mat& img, int quality) {
    std::vector<unsigned char> jpg;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    bool ok = cv::imencode(".jpg", img, jpg, params);
    if (!ok || jpg.empty()) return "";
    std::string id = randomHexId();
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_results[id] = std::move(jpg);
        g_resultOrder.push(id);
        while (g_resultOrder.size() > kMaxCachedResults) {
            g_results.erase(g_resultOrder.front());
            g_resultOrder.pop();
        }
    }
    return id;
}

std::string modeName(FixMode m) {
    switch (m) {
        case FixMode::Perspective:
            return "perspective";
        case FixMode::Deskew:
            return "deskew";
        default:
            return "none";
    }
}

std::vector<unsigned char> toJpegBytes(const cv::Mat& img, int quality) {
    std::vector<unsigned char> jpg;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
    if (!cv::imencode(".jpg", img, jpg, params)) return {};
    return jpg;
}

cv::Mat ocrPrepared(const cv::Mat& img, bool fast) {
    cv::Mat out = img;
    int targetSide = fast ? 1920 : 2560;
    int longSide = std::max(out.cols, out.rows);
    if (longSide > targetSide) {
        double s = targetSide * 1.0 / longSide;
        cv::resize(out, out, cv::Size(static_cast<int>(out.cols * s),
                                      static_cast<int>(out.rows * s)));
    }
    return out;
}

}  // namespace

int main() {
    std::string exeDir = exeDirectory();
    std::string webDir = exeDir + "/web";
    std::string configPath = exeDir + "/config.json";

    AppConfig g_config;
    if (!loadConfig(configPath, g_config)) {
        std::cout << "boardnote: config.json not found, online features disabled"
                  << std::endl;
    } else {
        std::cout << "boardnote: config loaded, baidu=" << g_config.baiduReady()
                  << " llm=" << g_config.llmReady() << std::endl;
    }

    httplib::Server svr;

    svr.Get("/api/ping", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    svr.Post("/api/correct", [&](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json doc;
        doc["results"] = nlohmann::json::array();

        const std::vector<httplib::FormData>& files = req.form.get_files("images");
        int accepted = 0;
        int rejected = 0;
        for (const auto& file : files) {
            if (accepted + rejected >= kMaxFiles) break;
            if (file.content.size() > kMaxFileBytes) {
                ++rejected;
                continue;
            }
            std::vector<unsigned char> raw(file.content.begin(), file.content.end());
            cv::Mat img = cv::imdecode(raw, cv::IMREAD_COLOR);
            if (img.empty()) {
                ++rejected;
                continue;
            }
            FixResult fixed = processPhoto(img);
            std::string id = storeJpeg(fixed.out, 92);
            if (id.empty()) {
                ++rejected;
                continue;
            }
            ++accepted;
            nlohmann::json item;
            item["id"] = id;
            item["url"] = "/result/" + id;
            item["name"] = file.filename;
            item["width"] = fixed.out.cols;
            item["height"] = fixed.out.rows;
            item["mode"] = modeName(fixed.mode);
            item["angle"] = fixed.angleDeg;
            if (fixed.mode == FixMode::Perspective && fixed.corners.size() == 4) {
                item["corners"] = nlohmann::json::array();
                for (const auto& c : fixed.corners) {
                    item["corners"].push_back({{"x", c.x}, {"y", c.y}});
                }
            }
            doc["results"].push_back(std::move(item));
        }
        doc["accepted"] = accepted;
        doc["rejected"] = rejected;
        res.set_content(doc.dump(), "application/json");
    });

    svr.Post("/api/correct_manual", [&](const httplib::Request& req,
                                        httplib::Response& res) {
        nlohmann::json doc;
        const std::vector<httplib::FormData>& files = req.form.get_files("image");
        if (files.empty() || files[0].content.size() > kMaxFileBytes) {
            res.status = 400;
            doc["error"] = "bad request";
            res.set_content(doc.dump(), "application/json");
            return;
        }
        std::vector<unsigned char> raw(files[0].content.begin(), files[0].content.end());
        cv::Mat img = cv::imdecode(raw, cv::IMREAD_COLOR);
        if (img.empty()) {
            res.status = 400;
            doc["error"] = "cannot decode image";
            res.set_content(doc.dump(), "application/json");
            return;
        }

        std::vector<cv::Point2f> corners;
        std::string cornersStr = req.form.get_field("corners");
        try {
            nlohmann::json cj = nlohmann::json::parse(cornersStr);
            if (cj.is_array() && cj.size() == 4) {
                for (const auto& p : cj) {
                    corners.emplace_back(p[0].get<float>(), p[1].get<float>());
                }
            }
        } catch (...) {
            res.status = 400;
            doc["error"] = "bad corners";
            res.set_content(doc.dump(), "application/json");
            return;
        }

        cv::Mat out = processManual(img, corners);
        if (out.empty()) {
            res.status = 400;
            doc["error"] = "invalid corners";
            res.set_content(doc.dump(), "application/json");
            return;
        }

        std::string id = storeJpeg(out, 92);
        doc["url"] = "/result/" + id;
        doc["width"] = out.cols;
        doc["height"] = out.rows;
        res.set_content(doc.dump(), "application/json");
    });

    svr.Get(R"(/result/([0-9a-f]+))", [](const httplib::Request& req,
                                        httplib::Response& res) {
        std::string id = req.matches[1];
        std::lock_guard<std::mutex> lock(g_resultMutex);
        auto it = g_results.find(id);
        if (it == g_results.end()) {
            res.status = 404;
            res.set_content("not found", "text/plain");
            return;
        }
        res.set_content(reinterpret_cast<const char*>(it->second.data()),
                        it->second.size(), "image/jpeg");
    });

    svr.Post("/api/note", [&](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json doc;
        const std::vector<httplib::FormData>& files = req.form.get_files("image");
        if (files.empty() || files[0].content.size() > kMaxFileBytes) {
            res.status = 400;
            doc["error"] = "bad request";
            res.set_content(doc.dump(), "application/json");
            return;
        }

        std::vector<unsigned char> raw(files[0].content.begin(),
                                       files[0].content.end());
        cv::Mat img = cv::imdecode(raw, cv::IMREAD_COLOR);
        if (img.empty()) {
            res.status = 400;
            doc["error"] = "cannot decode image";
            res.set_content(doc.dump(), "application/json");
            return;
        }

        auto t0 = std::chrono::steady_clock::now();
        bool fastMode = req.has_param("fast") && req.get_param_value("fast") == "1";

        FixResult fixed = processPhoto(img);
        std::string id = storeJpeg(fixed.out, 92);
        doc["imageUrl"] = id.empty() ? "" : "/result/" + id;
        doc["mode"] = modeName(fixed.mode);
        doc["blur"] = fixed.blurScore;
        auto tFix = std::chrono::steady_clock::now();

        std::string quadId = storeJpeg(fixed.quadDebug, 88);
        std::string rawId = storeJpeg(fixed.correctedRaw, 90);
        doc["steps"] = nlohmann::json::array();
        if (!quadId.empty()) {
            doc["steps"].push_back({{"label", "角点检测"},
                                    {"url", "/result/" + quadId}});
        }
        if (!rawId.empty()) {
            doc["steps"].push_back({{"label", "几何矫正"},
                                    {"url", "/result/" + rawId}});
        }
        if (!id.empty()) {
            doc["steps"].push_back({{"label", "清晰化增强"},
                                    {"url", "/result/" + id}});
        }

        std::cout << "[step1] 收到图片 " << img.cols << "x" << img.rows
                  << " 矫正模式=" << modeName(fixed.mode)
                  << (fastMode ? " 快速模式" : "")
                  << " 耗时=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     tFix - t0)
                                     .count()
                  << "ms" << std::endl;

        std::vector<unsigned char> ocrJpg = toJpegBytes(ocrPrepared(fixed.out, fastMode),
                                                        fastMode ? 85 : 90);

        if (!g_config.baiduReady()) {
            doc["ocr"]["status"] = "no_key";
            std::cout << "[step2] OCR: 未配置密钥，跳过" << std::endl;
        } else {
            std::string token;
            std::string err;
            if (!fetchBaiduToken(g_config, token, err)) {
                doc["ocr"]["status"] = "error";
                doc["ocr"]["detail"] = "token: " + err;
                std::cout << "[step2] OCR token 失败: " << err << std::endl;
            } else {
                std::cout << "[step2] 获取百度 token 成功，调用"
                          << (fastMode ? "通用识别(快速)" : "手写识别") << "…"
                          << std::endl;
                OcrResult r = baiduOcr(g_config, ocrJpg, token, !fastMode);
                if (!r.ok && !fastMode) {
                    std::cout << "[step3] 手写识别未出结果(" << r.detail
                              << ")，降级通用识别…" << std::endl;
                    r = baiduOcr(g_config, ocrJpg, token, false);
                }
                auto tOcr = std::chrono::steady_clock::now();
                std::cout << "[step3] OCR 完成，字符数=" << (r.ok ? r.text.size() : 0)
                          << " 耗时="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 tOcr - tFix)
                                 .count()
                          << "ms" << std::endl;
                doc["ocr"]["status"] = r.status;
                if (r.ok) {
                    doc["ocr"]["text"] = r.text;
                } else {
                    doc["ocr"]["detail"] = r.detail;
                }
                if (r.ok && g_config.llmReady()) {
                    std::cout << "[step4] 调用大模型整理笔记…" << std::endl;
                    LlmResult note = organizeNotes(g_config, r.text,
                                                   fastMode ? 600 : 1200);
                    auto tLlm = std::chrono::steady_clock::now();
                    doc["note"]["status"] = note.ok ? "ok" : "error";
                    if (note.ok) {
                        doc["note"]["markdown"] = note.content;
                        std::cout << "[step4] 笔记整理完成 耗时="
                                  << std::chrono::duration_cast<
                                         std::chrono::milliseconds>(tLlm - tOcr)
                                         .count()
                                  << "ms" << std::endl;
                    } else {
                        doc["note"]["detail"] = note.detail;
                        std::cout << "[step4] 笔记整理失败: " << note.detail
                                  << std::endl;
                    }
                } else if (r.ok && !g_config.llmReady()) {
                    doc["note"]["status"] = "no_key";
                    std::cout << "[step4] LLM 未配置，仅输出识别原文" << std::endl;
                }
            }
        }
        res.set_content(doc.dump(), "application/json");
    });

    svr.set_mount_point("/", webDir);
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                 std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(std::string("server error: ") + e.what(), "text/plain");
        }
    });

    std::cout << "boardnote: open http://127.0.0.1:" << kPort << std::endl;
    std::thread browserOpener([]() {
        Sleep(800);
        ShellExecuteW(nullptr, L"open", L"http://127.0.0.1:8080", nullptr, nullptr,
                      SW_SHOWNORMAL);
    });
    browserOpener.detach();
    if (!svr.listen("127.0.0.1", kPort)) {
        std::cerr << "boardnote: failed to listen on port " << kPort << std::endl;
        return 1;
    }
    return 0;
}
