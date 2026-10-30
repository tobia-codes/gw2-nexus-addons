// HttpWinInet.cpp — see HttpWinInet.h.
#include "net/HttpWinInet.h"

#include <windows.h>
#include <wininet.h>

namespace Trailpack { namespace Core { namespace Http {

namespace {

std::string s_UserAgent = "TrailpackCore (+https://github.com/tobia-codes/gw2-nexus-addons)";

// Response bodies are capped: a batch of 200 items is well below this.
constexpr size_t kMaxBody = 16u * 1024u * 1024u;

} // namespace

void SetUserAgent(const std::string& userAgent)
{
    s_UserAgent = userAgent;
}

int Get(const std::string& url, std::string& body)
{
    body.clear();
    HINTERNET net = InternetOpenA(s_UserAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (net == nullptr) return 0;
    DWORD timeout = 30000;
    InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof timeout);
    InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    BOOL decode = TRUE;                                  // let WinINet undo gzip
    InternetSetOptionA(net, INTERNET_OPTION_HTTP_DECODING, &decode, sizeof decode);

    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES |
                        INTERNET_FLAG_NO_UI | INTERNET_FLAG_SECURE;
    const char* headers = "Accept: application/json\r\nAccept-Encoding: gzip\r\n";
    HINTERNET req = InternetOpenUrlA(net, url.c_str(), headers, (DWORD)-1, flags, 0);
    if (req == nullptr) {
        InternetCloseHandle(net);
        return 0;
    }

    DWORD status = 0, len = sizeof status;
    HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);

    char buf[16384];
    DWORD read = 0;
    bool ok = true;
    while (InternetReadFile(req, buf, sizeof buf, &read) && read > 0) {
        if (body.size() + read > kMaxBody) { ok = false; break; }
        body.append(buf, read);
    }
    InternetCloseHandle(req);
    InternetCloseHandle(net);
    if (!ok) {
        body.clear();
        return 0;
    }
    return (int)status;
}

}}} // namespace Trailpack::Core::Http
