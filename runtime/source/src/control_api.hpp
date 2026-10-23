#pragma once
#include "control_protocol.hpp"
#include <functional>
#include <memory>

namespace control {
struct Settings {
    bool enabled = true;
    int port = 7778;
    std::string key;
    bool cors = false;
    static Settings load(const std::string& envPath = ".env");
};
Json success(const Json& value);
Json failure(int status, const std::string& message);
using Handler = std::function<Json(const std::string&, const Json&)>;
class Server {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Server(const Settings&, Handler);
    ~Server();
    int port() const;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
};
Json http(const Settings&, const std::string& method, const std::string& path, const Json& body = nullptr);
int mcp(const Settings&);
}
