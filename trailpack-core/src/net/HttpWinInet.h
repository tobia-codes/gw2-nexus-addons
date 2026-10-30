#pragma once
// HttpWinInet.h — the HttpFetch of the DLL: an HTTPS GET via WinINet with Core's User-Agent
// (PLAN D10). Blocking; called only from the worker thread.

#include <string>

namespace Trailpack { namespace Core { namespace Http {

// Returns the HTTP status (0 = no connection / timeout) and the body.
int Get(const std::string& url, std::string& body);

// "TrailpackCore/<version> (+https://github.com/tobia-codes/gw2-nexus-addons)"; set once at load.
void SetUserAgent(const std::string& userAgent);

}}} // namespace Trailpack::Core::Http
