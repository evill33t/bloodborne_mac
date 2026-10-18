// A small synchronous HTTP client for the host's own traffic to the private
// server (the session service). The game's own sceHttp calls go
// through hle/http.cpp; this one never touches guest state and may be used
// from any host thread.
#pragma once

#include "replay/json.h"

#include <string>

namespace net {

struct HttpResult {
    long status = 0;      // HTTP status, 0 when the transport failed
    std::string body;
    std::string error;    // transport error text, empty on success
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// online.np_server from the config, else "<online.scheme>://<online.host>:18671".
std::string np_server_base();
// online.auth_server, else np_server_base(): the origin the account calls go to.
std::string auth_server_base();
// POST to "<auth_server_base><path>", otherwise as np_post.
bool auth_post(const std::string& path, const json::Value& body, json::Value& out, std::string& error,
               int timeout_ms = 8000);

HttpResult http_get(const std::string& url, int timeout_ms = 5000);
HttpResult http_post_json(const std::string& url, const json::Value& body, int timeout_ms = 5000);

// POST/GET to "<np_server_base><path>" and parse the JSON reply. Returns
// false (with `error`) on a transport error, a non-2xx status, or a body
// that is not JSON; `out` holds the parsed object on success.
bool np_post(const std::string& path, const json::Value& body, json::Value& out, std::string& error,
             int timeout_ms = 5000);
bool np_get(const std::string& path, json::Value& out, std::string& error, int timeout_ms = 5000);

// Reads that never throw: the member or a default.
std::string str_of(const json::Value& obj, const char* key, const std::string& dflt = "");
long long int_of(const json::Value& obj, const char* key, long long dflt = 0);

}  // namespace net
