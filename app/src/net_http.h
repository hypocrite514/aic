#pragma once
#include <string>
#include <utility>
#include <vector>

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;

    bool ok() const { return status >= 200 && status < 300; }
};

HttpResponse httpsRequest(const std::string& method, const std::string& url,
                          const std::string& body = "",
                          const std::string& contentType = "",
                          const std::vector<std::pair<std::string, std::string>>&
                              headers = {});

std::string urlEncode(const std::string& s);

std::string base64Encode(const std::vector<unsigned char>& data);
