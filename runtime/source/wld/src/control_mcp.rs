//! Stdio MCP adapter. All operations go through the running overlay's HTTP API.
use crate::{
    client::{ApiError, Result},
    control_http::Settings,
    control_protocol,
};
use serde_json::{Value, json};
use std::{
    io::{BufRead, Read, Write},
    net::TcpStream,
    sync::OnceLock,
    time::Duration,
};

pub fn contract() -> &'static Value {
    static CONTRACT: OnceLock<Value> = OnceLock::new();
    CONTRACT.get_or_init(|| {
        serde_json::from_str(include_str!("../../mcp/contract.json")).expect("MCP contract")
    })
}
fn recovery(status: u16) -> Value {
    contract()["recovery"]
        .get(status.to_string())
        .unwrap_or(&contract()["recovery"]["default"])
        .clone()
}
fn rpc_error(id: Value, code: i32, message: &str, data: Value) -> Value {
    json!({"jsonrpc":"2.0","id":id,"error":{"code":code,"message":message,"data":data}})
}

pub fn http(settings: &Settings, method: &str, path: &str, body: &Value) -> Result<Value> {
    let mut stream = TcpStream::connect_timeout(&settings.address, Duration::from_secs(2))
        .map_err(|_| ApiError::new(503, "Start the Macrohelp overlay first"))?;
    stream
        .set_read_timeout(Some(Duration::from_secs(20)))
        .map_err(|e| ApiError::new(503, e.to_string()))?;
    stream
        .set_write_timeout(Some(Duration::from_secs(5)))
        .map_err(|e| ApiError::new(503, e.to_string()))?;
    let body = if method == "GET" {
        String::new()
    } else {
        body.to_string()
    };
    write!(stream,"{method} {path} HTTP/1.1\r\nHost: {}\r\nAuthorization: Bearer {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",settings.address,settings.key,body.len()).map_err(|_|ApiError::new(503,"Overlay control connection closed"))?;
    let mut bytes = Vec::new();
    stream
        .take((control_protocol::LIMIT * 4 + 16385) as u64)
        .read_to_end(&mut bytes)
        .map_err(|_| ApiError::new(503, "Overlay control response failed"))?;
    if bytes.len() > control_protocol::LIMIT * 4 + 16384 {
        return Err(ApiError::new(502, "Overlay response exceeds 4 MiB"));
    }
    let end = bytes
        .windows(4)
        .position(|s| s == b"\r\n\r\n")
        .ok_or_else(|| ApiError::new(502, "Invalid overlay response"))?;
    let head = std::str::from_utf8(&bytes[..end])
        .map_err(|_| ApiError::new(502, "Invalid overlay headers"))?;
    let status = head
        .split_whitespace()
        .nth(1)
        .and_then(|n| n.parse::<u16>().ok())
        .ok_or_else(|| ApiError::new(502, "Missing overlay status"))?;
    let result: Value = serde_json::from_slice(&bytes[end + 4..])
        .map_err(|_| ApiError::new(502, "Invalid overlay JSON"))?;
    if status >= 400 || result["ok"] != true {
        return Err(ApiError::new(
            status,
            result["error"]["message"]
                .as_str()
                .unwrap_or("Overlay control failed"),
        ));
    }
    Ok(result["result"].clone())
}
pub fn tools() -> Value {
    let empty = json!({"type":"object","properties":{},"additionalProperties":false});
    Value::Array(
        contract()["tools"]
            .as_array()
            .unwrap()
            .iter()
            .map(|tool| {
                let mut tool = tool.clone();
                tool.as_object_mut().unwrap().remove("method");
                tool.as_object_mut().unwrap().remove("route");
                tool["inputSchema"] = if tool["name"] == "macrohelp_control" {
                    control_protocol::schema()["request"].clone()
                } else {
                    empty.clone()
                };
                tool
            })
            .collect(),
    )
}
pub struct Session {
    initialized: bool,
}
impl Default for Session {
    fn default() -> Self {
        Self { initialized: false }
    }
}
impl Session {
    pub fn dispatch(
        &mut self,
        request: &Value,
        call: &dyn Fn(&str, &str, &Value) -> Result<Value>,
    ) -> Option<Value> {
        let id = request.get("id").cloned();
        if request["jsonrpc"] != "2.0"
            || !request["method"].is_string()
            || id
                .as_ref()
                .is_some_and(|id| !id.is_string() && !id.is_number() && !id.is_null())
        {
            let id = id
                .filter(|id| id.is_string() || id.is_number())
                .unwrap_or(Value::Null);
            return Some(rpc_error(
                id,
                -32600,
                "Invalid JSON-RPC request",
                json!({"next":contract()["rpc_recovery"]}),
            ));
        }
        let id = id?;
        if request.get("params").is_some_and(|p| !p.is_object()) {
            return Some(rpc_error(
                id,
                -32602,
                "MCP params must be an object",
                json!({"next":contract()["rpc_recovery"]}),
            ));
        }
        let result = match request["method"].as_str().unwrap() {
            "initialize" => {
                if request["params"]
                    .get("protocolVersion")
                    .is_some_and(|v| !v.is_string())
                {
                    return Some(rpc_error(
                        id,
                        -32602,
                        "protocolVersion must be a string",
                        json!({"next":contract()["rpc_recovery"]}),
                    ));
                }
                self.initialized = true;
                let version = request["params"]["protocolVersion"].as_str().unwrap_or("");
                let version = if ["2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"]
                    .contains(&version)
                {
                    version
                } else {
                    "2025-11-25"
                };
                Ok(
                    json!({"protocolVersion":version,"capabilities":{"tools":{"listChanged":false},"resources":{"subscribe":false,"listChanged":false}},"serverInfo":{"name":"macrohelp-overlay","version":"1.1.0"},"instructions":contract()["instructions"]}),
                )
            }
            "ping" => Ok(json!({})),
            _ if !self.initialized => Err((-32002, "Initialize MCP first")),
            "tools/list" => Ok(json!({"tools":tools()})),
            "resources/list" => {
                let resources: Vec<_> = contract()["resources"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .map(|r| {
                        let mut r = r.clone();
                        r.as_object_mut().unwrap().remove("route");
                        r
                    })
                    .collect();
                Ok(json!({"resources":resources}))
            }
            "resources/read" => {
                let Some(uri) = request["params"]["uri"].as_str() else {
                    return Some(rpc_error(
                        id,
                        -32602,
                        "Resource uri must be a string",
                        json!({"next":contract()["rpc_recovery"]}),
                    ));
                };
                if let Some(resource) = contract()["resources"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .find(|r| r["uri"] == uri)
                {
                    let text = if let Some(route) = resource["route"].as_str() {
                        match call("GET", route, &json!({})) {
                            Ok(value) => {
                                if resource["mimeType"] == "text/plain" {
                                    let Some(text) = value["text"].as_str() else {
                                        return Some(rpc_error(
                                            id,
                                            -32603,
                                            "Invalid overlay Help response",
                                            json!({"next":recovery(503)}),
                                        ));
                                    };
                                    text.to_owned()
                                } else {
                                    value.to_string()
                                }
                            }
                            Err(error) => {
                                return Some(rpc_error(
                                    id,
                                    -32603,
                                    &error.message,
                                    json!({"status":error.status,"next":recovery(error.status)}),
                                ));
                            }
                        }
                    } else {
                        include_str!("../../../../skills/macrohelp/SKILL.md").to_owned()
                    };
                    Ok(
                        json!({"contents":[{"uri":uri,"mimeType":resource["mimeType"],"text":text}]}),
                    )
                } else {
                    Err((-32002, "Unknown resource; use resources/list"))
                }
            }
            "tools/call" => {
                let args = request["params"]
                    .get("arguments")
                    .cloned()
                    .unwrap_or_else(|| json!({}));
                let tool = contract()["tools"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .find(|t| t["name"] == request["params"]["name"]);
                if let Some(tool) = tool {
                    let method = tool["method"].as_str().unwrap();
                    let path = tool["route"].as_str().unwrap();
                    let response = if !args.is_object() {
                        Err(ApiError::new(400, "Tool arguments must be an object"))
                    } else if path == "/v1/control" {
                        control_protocol::normalize(&args)
                            .and_then(|args| call(method, path, &args))
                    } else if !args.as_object().unwrap().is_empty() {
                        Err(ApiError::new(400, "This tool accepts no arguments"))
                    } else {
                        call(method, path, &args)
                    };
                    let (value, is_error) = match response {
                        Ok(value) => (value, false),
                        Err(error) => (
                            json!({"error":{"status":error.status,"message":error.message,"next":recovery(error.status)}}),
                            true,
                        ),
                    };
                    Ok(
                        json!({"content":[{"type":"text","text":value.to_string()}],"structuredContent":value,"isError":is_error}),
                    )
                } else {
                    Err((-32602, "Unknown tool; use tools/list"))
                }
            }
            _ => Err((-32601, "Method not found")),
        };
        Some(match result {
            Ok(result) => json!({"jsonrpc":"2.0","id":id,"result":result}),
            Err((code, message)) => rpc_error(
                id,
                code,
                message,
                json!({"next":contract()["rpc_recovery"]}),
            ),
        })
    }
}
pub fn run(settings: &Settings) -> Result<()> {
    let mut session = Session::default();
    let mut input = std::io::stdin().lock();
    let mut output = std::io::stdout().lock();
    loop {
        let mut line = String::new();
        let limit = control_protocol::LIMIT + 16384;
        let n = input
            .by_ref()
            .take((limit + 1) as u64)
            .read_line(&mut line)
            .map_err(|_| ApiError::new(400, "Invalid MCP input"))?;
        if n == 0 {
            break;
        }
        if n > limit {
            return Err(ApiError::new(413, "MCP message exceeds 1 MiB"));
        }
        let response = match serde_json::from_str::<Value>(&line) {
            Ok(value) => session.dispatch(&value, &|method, path, body| {
                http(settings, method, path, body)
            }),
            Err(_) => Some(rpc_error(
                Value::Null,
                -32700,
                "Parse error",
                json!({"next":contract()["rpc_recovery"]}),
            )),
        };
        if let Some(response) = response {
            writeln!(output, "{response}")
                .and_then(|_| output.flush())
                .map_err(|_| ApiError::new(503, "MCP output closed"))?;
        }
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn lifecycle_and_tools_forward_without_a_second_executor() {
        let mut session = Session::default();
        let call = |method: &str, path: &str, body: &Value| {
            assert_eq!(method, "POST");
            assert_eq!(path, "/v1/control");
            assert_eq!(body["no_replace"], true);
            Ok(json!({"run":"one"}))
        };
        assert!(
            session
                .dispatch(
                    &json!({"jsonrpc":"2.0","id":1,"method":"tools/list"}),
                    &call
                )
                .unwrap()
                .get("error")
                .is_some()
        );
        assert!(session.dispatch(&json!({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}),&call).unwrap()["result"]["capabilities"]["tools"].is_object());
        assert!(
            session
                .dispatch(
                    &json!({"jsonrpc":"2.0","method":"notifications/initialized"}),
                    &call
                )
                .is_none()
        );
        assert_eq!(
            session
                .dispatch(
                    &json!({"jsonrpc":"2.0","id":3,"method":"tools/list"}),
                    &call
                )
                .unwrap()["result"]["tools"]
                .as_array()
                .unwrap()
                .len(),
            7
        );
        let result=session.dispatch(&json!({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"macrohelp_control","arguments":{"menu":2,"no_replace":true}}}),&call).unwrap();
        assert_eq!(result["result"]["structuredContent"]["run"], "one");
        assert_eq!(result["result"]["isError"], false);
        let result=session.dispatch(&json!({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"macrohelp_control","arguments":{}}}),&|_,_,_|Err(ApiError::new(409,"busy"))).unwrap();
        assert_eq!(result["result"]["isError"], true);
    }
}
