#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define CPPHTTPLIB_HEADER_MAX_LENGTH 16384
#define CPPHTTPLIB_HEADER_MAX_COUNT 64
#include "../../../http-daemon/include/httplib.h"
#include "control_api.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace control {
Json success(const Json& v) { return {{"ok", true}, {"result", v}}; }
Json failure(int status, const std::string& message) { return {{"ok", false}, {"error", {{"status", status}, {"message", message}}}}; }
static bool equalKey(const std::string& a, const std::string& b) {
    size_t diff = a.size() ^ b.size();
    for (size_t i = 0; i < b.size(); ++i) diff |= (i < a.size() ? static_cast<unsigned char>(a[i]) : 0) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}
namespace detail {
std::string privateKeyFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    HANDLE token = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != 0, "Cannot read process identity", 500);
    DWORD count = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &count);
    std::vector<unsigned char> user(count);
    bool userOk = GetTokenInformation(token, TokenUser, user.data(), count, &count) != 0;
    CloseHandle(token);
    require(userOk, "Cannot read process identity", 500);
    LPWSTR sid = nullptr;
    require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid) != 0, "Cannot format process identity", 500);
    auto dacl = std::wstring(L"D:P(A;;FA;;;SY)(A;;FA;;;") + sid + L")"; LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != 0, "Cannot secure control key", 500);
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | WRITE_DAC, 0, &attributes, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) { LocalFree(descriptor); throw Error(500, "Cannot open private control key"); }
    BY_HANDLE_FILE_INFORMATION info = {}; LARGE_INTEGER size = {};
    bool valid = GetFileInformationByHandle(file, &info) && GetFileSizeEx(file, &size)
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 && size.QuadPart <= 256
        && SetKernelObjectSecurity(file, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    if (!valid) { CloseHandle(file); throw Error(500, "Unsafe control key file"); }
    std::string key; DWORD bytes = 0;
    if (size.QuadPart) {
        key.resize(static_cast<size_t>(size.QuadPart));
        valid = ReadFile(file, key.data(), static_cast<DWORD>(key.size()), &bytes, nullptr) && bytes == key.size();
        key = trim(key);
    } else {
        unsigned char random[32];
        valid = BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
        const char* hex = "0123456789abcdef";
        if (valid) for (auto b : random) { key += hex[b >> 4]; key += hex[b & 15]; }
        if (valid) valid = WriteFile(file, key.data(), static_cast<DWORD>(key.size()), &bytes, nullptr) && bytes == key.size() && FlushFileBuffers(file);
    }
    CloseHandle(file);
    require(valid && key.size() >= 32, "Cannot read or create control key", 500);
    return key;
}
}
static std::string privateKey() {
    wchar_t* folder = nullptr;
    require(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)), "Cannot find private state directory", 500);
    auto path = std::filesystem::path(folder) / L"Macrohelp" / L"control-key";
    CoTaskMemFree(folder);
    return detail::privateKeyFile(path);
}
Settings Settings::load(const std::string& envPath) {
    std::map<std::string, std::string> env;
    std::ifstream file(std::filesystem::u8path(envPath)); std::string line;
    while (std::getline(file, line)) {
        line = trim(line); if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
        auto eq = line.find('='); if (eq == std::string::npos || line[0] == '#') continue;
        auto value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'') && value.back() == value[0]) value = value.substr(1, value.size() - 2);
        env[trim(line.substr(0, eq))] = value;
    }
    auto get = [&](const char* name) { const char* v = std::getenv(name); return v ? std::string(v) : env[name]; };
    Settings settings; settings.enabled = get("MACROHELP_CONTROL_ENABLED") != "false";
    auto port = get("MACROHELP_CONTROL_PORT");
    if (!port.empty()) {
        require(port.find_first_not_of("0123456789") == std::string::npos && port.size() <= 5, "Invalid control port", 500);
        settings.port = std::stoi(port); require(settings.port > 0 && settings.port <= 65535, "Invalid control port", 500);
    }
    settings.key = get("MACROHELP_CONTROL_KEY");
    if (settings.enabled && settings.key.empty()) settings.key = privateKey();
    require(!settings.enabled || (settings.key.size() >= 24 && settings.key.size() <= 256 && std::all_of(settings.key.begin(),settings.key.end(),[](unsigned char c){return c>=33&&c<=126;})), "Invalid control key", 500);
    return settings;
}
struct Server::Impl {
    httplib::Server server;
    std::thread thread;
    int boundPort=0;
    Impl(const Settings& settings, Handler handler) {
        server.new_task_queue = [] { return new httplib::ThreadPool(4, 4, 8); };
        server.set_payload_max_length(Limit).set_read_timeout(5).set_write_timeout(5).set_keep_alive_max_count(1);
        server.set_default_headers({{"Cache-Control","no-store"},{"X-Content-Type-Options","nosniff"}});
        server.set_pre_routing_handler([settings,this](const httplib::Request& req, httplib::Response& res) {
            int code = 0; std::string message;
            size_t headerBytes=req.method.size()+req.target.size()+16;
            for(const auto& header:req.headers)headerBytes+=header.first.size()+header.second.size()+4;
            if(headerBytes>16384){code=431;message="Headers exceed 16 KiB";}
            if (req.get_header_value_count("Authorization") != 1 || !equalKey(req.get_header_value("Authorization"), "Bearer " + settings.key)) { code = 401; message = "Bearer authentication required"; }
            auto host = req.get_header_value("Host");
            std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if (!code && (req.get_header_value_count("Host") != 1 || (host != "127.0.0.1:" + std::to_string(boundPort) && host != "localhost:" + std::to_string(boundPort)))) { code = 400; message = "Invalid loopback Host"; }
            if (!code && (req.has_header("Transfer-Encoding") || req.get_header_value_count("Content-Length") > 1)) { code = 400; message = "Ambiguous request framing"; }
            auto type=req.get_header_value("Content-Type");type=trim(type.substr(0,type.find(';')));
            std::transform(type.begin(),type.end(),type.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if (!code && req.method == "POST" && type != "application/json") { code = 415; message = "Use application/json"; }
            if (!code) return httplib::Server::HandlerResponse::Unhandled;
            res.status = code; res.set_content(failure(code, message).dump(), "application/json");
            return httplib::Server::HandlerResponse::Handled;
        });
        auto route = [handler](const httplib::Request& req, httplib::Response& res) {
            try {
                Json body = Json::object();
                if (req.method == "POST" && !req.body.empty()) body = parse(req.body);
                if (req.path == "/v1/control") body = normalize(body);
                auto reply = req.path == "/v1/schema" ? success(schema()) : handler(req.path, body);
                res.status = reply.value("ok", false) ? 200 : reply.at("error").value("status", 500);
                res.set_content(reply.dump(), "application/json");
            } catch (const Error& e) { res.status = e.status; res.set_content(failure(e.status, e.what()).dump(), "application/json"); }
            catch (const Json::exception&) { res.status = 400; res.set_content(failure(400, "Invalid JSON").dump(), "application/json"); }
            catch (const std::exception&) { res.status = 500; res.set_content(failure(500, "Control request failed").dump(), "application/json"); }
        };
        for (const char* path : {"/v1/health", "/v1/state", "/v1/schema", "/v1/coords"}) server.Get(path, route);
        for (const char* path : {"/v1/control", "/v1/stop"}) server.Post(path, route);
        server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            if (res.body.empty()) res.set_content(failure(res.status, "HTTP request rejected").dump(), "application/json");
        });
        boundPort=settings.port;
        if(settings.port==0)boundPort=server.bind_to_any_port("127.0.0.1");
        else require(server.bind_to_port("127.0.0.1", settings.port), "Cannot bind overlay control port", 503);
        require(boundPort>0,"Cannot bind overlay control port",503);
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~Impl() { server.stop(); if (thread.joinable()) thread.join(); }
};
Server::Server(const Settings& s, Handler h) : impl(std::make_unique<Impl>(s, std::move(h))) {}
Server::~Server() = default;
int Server::port() const { return impl->boundPort; }
Json http(const Settings& settings, const std::string& method, const std::string& path, const Json& body) {
    httplib::Client client("127.0.0.1", settings.port);
    client.set_connection_timeout(2); client.set_read_timeout(20); client.set_write_timeout(5); client.set_max_timeout(20000); client.set_payload_max_length(4 * Limit);
    httplib::Headers headers = {{"Authorization", "Bearer " + settings.key}};
    auto result = method == "POST" ? client.Post(path, headers, body.dump(), "application/json") : client.Get(path, headers);
    require(static_cast<bool>(result), "Overlay control endpoint is unavailable", 503);
    return Json::parse(result->body);
}
Json schema() {
    Json s = {{"version",1},{"service","macrohelp-overlay"},{"menus",Menus},
        {"routes",{"GET /v1/health","GET /v1/schema","GET /v1/state","GET /v1/coords","POST /v1/control","POST /v1/stop"}},
        {"input_semantics","Each string submits one dialog input, including its Enter. Action objects address buttons. CSV starts with a menu; quote fields containing commas. Partial menus return a run ID; include run to continue that menu."},
        {"no_replace","Temporary config overrides for this run; otherwise save supplied fields. Omitted fields are retained. A continued run inherits no_replace."}};
    Json coordinate = {{"type","integer"},{"minimum",-100000},{"maximum",100000}};
    auto coords = [&](int n) { return Json{{"oneOf",Json::array({{{"type","null"}},{{"type","array"},{"minItems",n},{"maxItems",n},{"items",coordinate}}})}}; };
    Json sections = {{"script",{{"type","string"}}},{"registry",{{"type","string"}}}};
    std::map<std::string,std::vector<std::string>> groups = {{"vars",{"A","S","D","F"}},{"buffers",{"Z","X","C","V"}},{"zones",{"Z","X","C","V"}},{"shells",{"X","C","G","P","T"}},{"paths",{"import","export"}},{"view",{"crosshair","grid","coordinates","panel","keys","coordinates_only"}}};
    for (const auto& group : groups) {
        Json properties = Json::object();
        for (const auto& key : group.second) properties[key] = group.first == "zones" ? coords(4) : Json{{"type",group.first == "view" ? "boolean" : "string"}};
        sections[group.first] = {{"type","object"},{"properties",properties},{"additionalProperties",false}};
    }
    Json points = Json::object(); for (int i = 1; i <= 16; ++i) points[std::to_string(i)] = coords(2);
    sections["points"] = {{"type","object"},{"properties",points},{"additionalProperties",false}};
    Json action = {{"type","object"},{"properties",{{"action",{{"type","string"},{"enum",Actions}}},{"value",{{"type","string"}}},{"keys",{{"type","array"},{"items",{{"type","string"}}},{"minItems",1},{"maxItems",16}}},{"delay",{{"type","integer"},{"minimum",10},{"maximum",600000}}}}},{"required",{"action"}},{"additionalProperties",false}};
    s["request"] = {{"type","object"},{"properties",{
        {"menu",{{"oneOf",Json::array({{{"type","integer"},{"minimum",0},{"maximum",9}},{{"type","string"},{"enum",Menus}}})}}},
        {"inputs",{{"type","array"},{"maxItems",256},{"items",{{"oneOf",Json::array({{{"type","string"}},action})}}}}},
        {"config",{{"type","object"},{"properties",sections},{"additionalProperties",false}}},
        {"no_replace",{{"type","boolean"},{"description","Defaults false for new runs; omitted continuation values inherit the active run."}}},
        {"origin",{{"type","array"},{"items",coordinate},{"minItems",2},{"maxItems",2}}},
        {"run",{{"type","string"},{"description","The active run ID returned by a partial menu operation."}}}}},{"additionalProperties",false}};
    return s;
}
}
