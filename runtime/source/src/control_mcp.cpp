#include "control_api.hpp"
#include <iostream>

namespace control {
int mcp(const Settings& settings) {
    const std::vector<std::string> versions = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
    const std::map<std::string,std::string> routes = {{"macrohelp_control","/v1/control"},{"macrohelp_state","/v1/state"},{"macrohelp_stop","/v1/stop"},{"macrohelp_schema","/v1/schema"},{"macrohelp_health","/v1/health"}};
    std::string line;bool initialized=false;
    // Bounded line reads: never allocate an unlimited JSON-RPC frame.
    while (true) {
        line.clear(); char c; bool overflow = false;
        while (std::cin.get(c) && c != '\n') { if (line.size() < Limit) line += c; else overflow = true; }
        if (!std::cin && line.empty()) break;
        Json id = nullptr;
        try {
            require(!overflow, "MCP message exceeds 1 MiB", -32600);
            Json request = parse(line);
            require(request.is_object() && request.value("jsonrpc", "") == "2.0", "Invalid JSON-RPC request", -32600);
            if (!request.contains("id")) continue;
            id = request["id"]; Json result;
            require(id.is_null()||id.is_string()||id.is_number(),"Invalid JSON-RPC ID",-32600);
            auto method = request.value("method", ""); auto params = request.value("params", Json::object());
            if (method == "initialize") {
                initialized=true;
                auto version = params.value("protocolVersion", versions[0]);
                if (std::find(versions.begin(), versions.end(), version) == versions.end()) version = versions[0];
                result = {{"protocolVersion",version},{"capabilities",{{"tools",{{"listChanged",false}}}}},{"serverInfo",{{"name","macrohelp-overlay"},{"version","1.0.0"}}}};
            } else if (method == "ping") result = Json::object();
            else if (!initialized) throw Error(-32002,"Initialize MCP first");
            else if (method == "tools/list") {
                result = {{"tools",Json::array()}};
                for (const auto& route : routes) {
                    bool write = route.first == "macrohelp_control" || route.first == "macrohelp_stop";
                    result["tools"].push_back({{"name",route.first},{"description",write ? "Control the running Macrohelp overlay using its native menu flows." : "Read the running Macrohelp overlay."},
                        {"inputSchema",route.first == "macrohelp_control" ? schema()["request"] : Json{{"type","object"},{"additionalProperties",false}}},
                        {"annotations",{{"readOnlyHint",!write},{"destructiveHint",write},{"idempotentHint",route.first=="macrohelp_stop"||!write},{"openWorldHint",route.first=="macrohelp_control"}}}});
                }
            } else if (method == "tools/call") {
                auto name = params.value("name", ""); auto route = routes.find(name);
                require(route != routes.end(), "Unknown tool", -32602);
                auto arguments = params.value("arguments", Json::object());
                Json value;bool failed=false;
                try {
                    require(arguments.is_object(),"Tool arguments must be an object");
                    if (name == "macrohelp_control") arguments = normalize(arguments);
                    else require(arguments.empty(), "This tool takes no arguments");
                    auto reply=http(settings, name == "macrohelp_control" || name == "macrohelp_stop" ? "POST" : "GET", route->second, arguments);
                    failed=!reply.value("ok",false);value=failed?Json{{"error",reply.at("error")}}:reply.at("result");
                } catch (const std::exception& e) { auto detail=dynamic_cast<const Error*>(&e);failed=true;value={{"error",{{"status",detail?detail->status:503},{"message",e.what()}}}}; }
                result = {{"content",Json::array({{{"type","text"},{"text",value.dump()}}})},{"structuredContent",value},{"isError",failed}};
            } else throw Error(-32601, "Method not found");
            std::cout << Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump() << std::endl;
        } catch (const std::exception& e) {
            const auto* error = dynamic_cast<const Error*>(&e);
            int code = error && error->status < 0 ? error->status : dynamic_cast<const Json::parse_error*>(&e) ? -32700 : -32602;
            std::cout << Json{{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",e.what()}}}}.dump() << std::endl;
        }
    }
    return 0;
}
}
int main(int argc, char** argv) {
    try {
        std::string env = ".env";
        if (argc == 3 && std::string(argv[1]) == "--env") env = argv[2];
        else if (argc != 1) { std::cerr << "Usage: macrohelp-mcp [--env PATH]\n"; return 2; }
        auto settings = control::Settings::load(env);
        control::require(settings.enabled, "Overlay control is disabled", 503);
        return control::mcp(settings);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
