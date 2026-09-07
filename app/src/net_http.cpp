#include "net_http.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

bool parseUrl(const std::string& url, bool& secure, std::string& host,
              std::string& port, std::string& path) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    secure = url.rfind("https", 0) == 0;
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string hostport;
    if (pathStart == std::string::npos) {
        hostport = url.substr(hostStart);
        path = "/";
    } else {
        hostport = url.substr(hostStart, pathStart - hostStart);
        path = url.substr(pathStart);
    }
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    } else {
        host = hostport;
        port = secure ? "443" : "80";
    }
    return !host.empty();
}

}  // namespace

HttpResponse httpsRequest(const std::string& method, const std::string& url,
                          const std::string& body, const std::string& contentType,
                          const std::vector<std::pair<std::string, std::string>>&
                              headers) {
    HttpResponse res;

    bool secure = false;
    std::string host, port, path;
    if (!parseUrl(url, secure, host, port, path)) {
        res.error = "bad url";
        return res;
    }

    HINTERNET hSession = WinHttpOpen(L"boardnote/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    HINTERNET hConnect =
        WinHttpConnect(hSession, toWide(host).c_str(),
                       static_cast<INTERNET_PORT>(std::stoi(port)), 0);
    if (!hConnect) {
        res.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return res;
    }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, toWide(method).c_str(), toWide(path).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        res.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return res;
    }

    WinHttpSetTimeouts(hRequest, 30000, 30000, 60000, 60000);

    std::string headerStr;
    if (!contentType.empty()) headerStr += "Content-Type: " + contentType + "\r\n";
    for (const auto& h : headers) headerStr += h.first + ": " + h.second + "\r\n";

    LPCWSTR hdrs = headerStr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : toWide(headerStr).c_str();
    LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
    DWORD bodyLen = static_cast<DWORD>(body.size());

    BOOL ok = WinHttpSendRequest(hRequest, hdrs, headerStr.empty() ? 0 : -1L,
                                 bodyPtr, bodyLen, bodyLen, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    if (ok) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);
        res.status = static_cast<int>(status);

        DWORD available = 0;
        char buf[16384];
        while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
            DWORD toRead = available < sizeof(buf) ? available : sizeof(buf);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf, toRead, &read) || read == 0) break;
            res.body.append(buf, read);
        }
    } else {
        DWORD err = GetLastError();
        char msg[64];
        snprintf(msg, sizeof(msg), "WinHTTP error %lu", static_cast<unsigned long>(err));
        res.error = msg;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return res;
}

std::string urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string base64Encode(const std::vector<unsigned char>& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        unsigned int v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += table[v & 63];
    }
    size_t rem = data.size() - i;
    if (rem == 1) {
        unsigned int v = data[i] << 16;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        unsigned int v = (data[i] << 16) | (data[i + 1] << 8);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += '=';
    }
    return out;
}
