#pragma once
#include "../include/json.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace control {
using Json = nlohmann::json;
constexpr size_t Limit = 1024 * 1024;
inline const std::vector<std::string> Menus = {"save_cursor", "paste_buffers", "registry_hub", "circle_placer", "click_left", "click_right", "click_middle", "stop_all", "view_toggles", "record_keys"};
inline const std::vector<std::string> Actions = {"submit", "accept", "cancel", "play", "clear", "export", "import", "save_schedule", "paste", "paste_enter", "add_point", "save_click", "add_step", "capture", "manual", "add_paste", "replay"};
struct Error : std::runtime_error {
    int status;
    Error(int code, const std::string& message) : std::runtime_error(message), status(code) {}
};
inline void require(bool condition, const std::string& message, int status = 400) {
    if (!condition) throw Error(status, message);
}
inline std::string trim(std::string s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}
inline bool integer(const Json& v, int lo, int hi) {
    return v.is_number_integer() && v >= lo && v <= hi;
}
inline void fields(const Json& v, const std::vector<std::string>& allowed) {
    require(v.is_object(), "Expected an object");
    for (auto it = v.begin(); it != v.end(); ++it)
        require(std::find(allowed.begin(), allowed.end(), it.key()) != allowed.end(), "Unknown field: " + it.key());
}
inline void coordinates(const Json& v, size_t count) {
    require(v.is_array() && v.size() == count, "Invalid integer coordinates");
    for (const auto& n : v) {
        require(!n.is_string(), "Windows coordinates require integer physical pixels; board codes are Wayland-only. Use macrohelp_coords.");
        require(integer(n, -100000, 100000), "Invalid integer coordinates");
    }
}
inline void textSafety(const Json& v, unsigned depth = 0) {
    require(depth < 64, "JSON nesting exceeds 64 levels");
    if (v.is_string()) require(v.get_ref<const std::string&>().find('\0') == std::string::npos, "NUL text is not supported");
    else if (v.is_structured()) for (const auto& item : v) textSafety(item, depth + 1);
}
inline Json parse(const std::string& text) {
    require(text.size() <= Limit, "Control request exceeds 1 MiB", 413);
    // Abort before an untrusted deeply nested document can exhaust the stack.
    return Json::parse(text, [&](int d, Json::parse_event_t, Json&) {
        require(d < 64, "JSON nesting exceeds 64 levels");
        return true;
    });
}
inline Json csv(const std::string& text) {
    std::vector<std::pair<std::string, bool>> out;
    size_t p = 0;
    for (;;) {
        bool quoted = p < text.size() && text[p] == '"';
        std::string value;
        if (quoted) {
            ++p;
            bool closed = false;
            while (p < text.size()) {
                char c = text[p++];
                if (c == '"') {
                    if (p < text.size() && text[p] == '"') { ++p; value += '"'; }
                    else { closed = true; break; }
                } else value += c;
            }
            require(closed, "Unclosed CSV quote");
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
            require(p == text.size() || text[p] == ',', "Expected a comma after the quoted field");
        } else {
            while (p < text.size() && text[p] != ',') {
                require(text[p] != '"', "Quote the entire CSV field");
                value += text[p++];
            }
            value = trim(value);
        }
        out.emplace_back(value, quoted);
        require(out.size() <= 258, "Too many inputs");
        if (p == text.size()) break;
        ++p;
    }
    bool temporary = !out.empty() && !out.back().second && out.back().first == "no-replace";
    if (temporary) out.pop_back();
    require(!out.empty(), "Choose a menu");
    Json result = {{"menu", out[0].first}, {"inputs", Json::array()}, {"no_replace", temporary}};
    for (size_t i = 1; i < out.size(); ++i) result["inputs"].push_back(out[i].first);
    return result;
}
inline void patch(const Json& config) {
    fields(config, {"script", "registry", "points", "vars", "buffers", "zones", "shells", "paths", "view"});
    for (auto it = config.begin(); it != config.end(); ++it) {
        const auto& section = it.key(); const auto& values = it.value();
        if (section == "script" || section == "registry") { require(values.is_string(), "script and registry must be text"); continue; }
        if (section == "points") {
            require(values.is_object(), "points uses numbered keys 1–16");
            for (auto point = values.begin(); point != values.end(); ++point) {
                bool found = false;
                for (int i = 1; i <= 16; ++i) if (point.key() == std::to_string(i)) found = true;
                require(found, "Point number must be 1–16");
                if (!point.value().is_null()) coordinates(point.value(), 2);
            }
            continue;
        }
        auto keys = section == "vars" ? std::vector<std::string>{"A", "S", "D", "F"}
            : section == "shells" ? std::vector<std::string>{"X", "C", "G", "P", "T"}
            : section == "paths" ? std::vector<std::string>{"import", "export"}
            : section == "view" ? std::vector<std::string>{"crosshair", "grid", "coordinates", "panel", "keys", "coordinates_only"}
            : std::vector<std::string>{"Z", "X", "C", "V"};
        fields(values, keys);
        for (const auto& v : values) {
            if (section == "zones") { if (!v.is_null()) coordinates(v, 4); }
            else if (section == "view") require(v.is_boolean(), "View values must be boolean");
            else require(v.is_string(), "Config value must be text");
        }
    }
}
inline Json normalize(Json request) {
    textSafety(request);
    if (request.is_string()) request = csv(request.get<std::string>());
    fields(request, {"menu", "inputs", "config", "no_replace", "origin", "run"});
    if (request.contains("run")) require(request["run"].is_string() && !request["run"].get_ref<const std::string&>().empty() && request["run"].get_ref<const std::string&>().size() <= 80, "Invalid control run ID");
    if (request.contains("menu")) {
        auto& menu = request["menu"];
        if (menu.is_string()) {
            auto name = menu.get<std::string>(); int index = -1;
            for (int i = 0; i < 10; ++i) if (name == Menus[i] || name == std::to_string(i)) index = i;
            require(index >= 0, "Menu must be 0–9 or a canonical menu name"); menu = index;
        }
        require(integer(menu, 0, 9), "Menu must be 0–9 or a canonical menu name");
    }
    if (!request.contains("inputs")) request["inputs"] = Json::array();
    require(request["inputs"].is_array() && request["inputs"].size() <= 256, "inputs must be an array of at most 256 entries");
    require(request.contains("menu") || request.contains("run") || request["inputs"].empty(), "Choose a menu when supplying inputs");
    for (const auto& input : request["inputs"]) {
        if (input.is_string()) continue;
        fields(input, {"action", "value", "keys", "delay"});
        require(input.contains("action") && input["action"].is_string(), "Missing input action");
        auto action = input["action"].get<std::string>();
        require(std::find(Actions.begin(), Actions.end(), action) != Actions.end(), "Unknown input action");
        if (input.contains("value")) require(input["value"].is_string(), "Action value must be text");
        if (input.contains("keys")) {
            require(input["keys"].is_array() && !input["keys"].empty() && input["keys"].size() <= 16, "Choose 1–16 key names");
            for (const auto& key : input["keys"]) require(key.is_string() && !key.get_ref<const std::string&>().empty() && key.get_ref<const std::string&>().size() <= 128, "Choose 1–16 key names");
        }
        if (input.contains("delay")) require(integer(input["delay"], 10, 600000), "Step delay must be 10–600000 ms");
    }
    if (request.contains("no_replace")) require(request["no_replace"].is_boolean(), "no_replace must be boolean");
    if (!request.contains("config")) request["config"] = Json::object();
    patch(request["config"]);
    if (request.contains("origin")) coordinates(request["origin"], 2);
    require(request.dump().size() <= Limit, "Control request exceeds 1 MiB", 413);
    if (!request.contains("run") || request.contains("no_replace")) request["no_replace"] = request.value("no_replace", false);
    return request;
}
Json schema();
}
