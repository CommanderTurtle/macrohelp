#include "control_api.hpp"
#include <windows.h>
#include <iostream>

namespace control {
static std::string mcpAsset(int id) {
    auto module = GetModuleHandleW(nullptr);
    auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    require(resource != nullptr, "Missing MCP documentation resource", 503);
    auto data = LoadResource(module, resource);
    auto bytes = data ? LockResource(data) : nullptr;
    require(bytes != nullptr, "Cannot load MCP documentation resource", 503);
    return std::string(static_cast<const char*>(bytes), SizeofResource(module, resource));
}
int mcp(const Settings& settings) {
    const std::vector<std::string> versions = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
    const auto contract = Json::parse(mcpAsset(101));
    auto read = [&](const std::string& method, const std::string& path, const Json& body) {
        auto reply = http(settings, method, path, body);
        if (!reply.value("ok", false)) throw Error(reply.at("error").value("status",503), reply.at("error").value("message","Overlay control failed"));
        return reply.at("result");
    };
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
            require(request.is_object() && request.value("jsonrpc", Json()) == "2.0" && request.contains("method") && request["method"].is_string(), "Invalid JSON-RPC request", -32600);
            if(request.contains("id")) {
                const auto& candidate = request["id"];
                require(candidate.is_null()||candidate.is_string()||candidate.is_number(),"Invalid JSON-RPC ID",-32600);
                id=candidate;
            }
            if (!request.contains("id")) continue;
            Json result;
            auto method = request.value("method", ""); auto params = request.value("params", Json::object());
            require(params.is_object(), "MCP params must be an object", -32602);
            if (method == "initialize") {
                require(!params.contains("protocolVersion")||params["protocolVersion"].is_string(),"protocolVersion must be a string",-32602);
                auto version = params.value("protocolVersion", versions[0]);
                if (std::find(versions.begin(), versions.end(), version) == versions.end()) version = versions[0];
                initialized=true;
                result = {{"protocolVersion",version},{"capabilities",{{"tools",{{"listChanged",false}}},{"resources",{{"subscribe",false},{"listChanged",false}}}}},{"serverInfo",{{"name","macrohelp-overlay"},{"version","1.1.0"}}},{"instructions",contract["instructions"]}};
            } else if (method == "ping") result = Json::object();
            else if (!initialized) throw Error(-32002,"Initialize MCP first");
            else if (method == "tools/list") {
                result = {{"tools",Json::array()}};
                for (auto tool : contract["tools"]) {
                    tool.erase("method");tool.erase("route");
                    tool["inputSchema"]=tool["name"]=="macrohelp_control"?schema()["request"]:Json{{"type","object"},{"properties",Json::object()},{"additionalProperties",false}};
                    result["tools"].push_back(tool);
                }
            } else if (method == "resources/list") {
                result={{"resources",Json::array()}};
                for(auto resource:contract["resources"]){resource.erase("route");result["resources"].push_back(resource);}
            } else if (method == "resources/read") {
                require(params.contains("uri")&&params["uri"].is_string(),"Resource uri must be a string",-32602);
                auto found=std::find_if(contract["resources"].begin(),contract["resources"].end(),[&](const Json& r){return r["uri"]==params["uri"];});
                require(found!=contract["resources"].end(),"Unknown resource; use resources/list",-32002);
                std::string text;
                if(!found->contains("route"))text=mcpAsset(102);
                else {
                    auto value=read("GET",(*found)["route"],Json::object());
                    text=(*found)["mimeType"]=="text/plain"?value.at("text").get<std::string>():value.dump();
                }
                result={{"contents",Json::array({{{"uri",(*found)["uri"]},{"mimeType",(*found)["mimeType"]},{"text",text}}})}};
            } else if (method == "tools/call") {
                require(params.contains("name")&&params["name"].is_string(),"Tool name must be a string",-32602);
                auto name = params.value("name", "");
                auto tool=std::find_if(contract["tools"].begin(),contract["tools"].end(),[&](const Json& t){return t["name"]==name;});
                require(tool != contract["tools"].end(), "Unknown tool; use tools/list", -32602);
                auto arguments = params.value("arguments", Json::object());
                Json value;bool failed=false;
                try {
                    require(arguments.is_object(),"Tool arguments must be an object");
                    if (name == "macrohelp_control") arguments = normalize(arguments);
                    else require(arguments.empty(), "This tool takes no arguments");
                    value=read((*tool)["method"],(*tool)["route"],arguments);
                } catch (const std::exception& e) { auto detail=dynamic_cast<const Error*>(&e);failed=true;value={{"error",{{"status",detail?detail->status:503},{"message",e.what()}}}}; }
                if(failed)value["error"]["next"]=contract["recovery"].value(std::to_string(value["error"]["status"].get<int>()),contract["recovery"]["default"].get<std::string>());
                result = {{"content",Json::array({{{"type","text"},{"text",value.dump()}}})},{"structuredContent",value},{"isError",failed}};
            } else throw Error(-32601, "Method not found");
            std::cout << Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump() << std::endl;
        } catch (const std::exception& e) {
            const auto* error = dynamic_cast<const Error*>(&e);
            bool parseError=dynamic_cast<const Json::parse_error*>(&e)!=nullptr;
            int code = error && error->status < 0 ? error->status : parseError ? -32700 : -32603;
            Json data={{"next",contract["rpc_recovery"]}};
            if(error&&error->status>0)data={{"status",error->status},{"next",contract["recovery"].value(std::to_string(error->status),contract["recovery"]["default"].get<std::string>())}};
            std::cout << Json{{"jsonrpc","2.0"},{"id",id},{"error",{{"code",code},{"message",parseError?"Parse error":e.what()},{"data",data}}}}.dump() << std::endl;
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
