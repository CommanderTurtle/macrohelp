//! Real MCP subprocess -> real loopback HTTP -> fake UI boundary. No input injection.
use macrohelp_overlay::control_http::{Dispatch, Server, Settings};
use macrohelp_overlay::{client::ApiError, control_mcp, control_protocol};
use serde_json::{Value, json};
use std::{
    io::Write,
    process::{Command, Stdio},
    sync::{Arc, Mutex},
};
#[test]
fn mcp_stdio_speaks_json_only_and_uses_the_http_contract() {
    exercise(
        std::ffi::OsStr::new(env!("CARGO_BIN_EXE_macrohelpctl")),
        false,
    );
}
#[test]
fn native_mcp_uses_the_identical_http_contract_when_built() {
    if let Some(exe) = std::env::var_os("MACROHELP_CPP_MCP") {
        exercise(&exe, true);
    }
}
fn exercise(executable: &std::ffi::OsStr, native: bool) {
    let calls = Arc::new(Mutex::new(Vec::new()));
    let seen = calls.clone();
    let dispatch: Dispatch = Arc::new(move |method, path, body| {
        seen.lock()
            .unwrap()
            .push((method.to_owned(), path.to_owned(), body.clone()));
        if path == "/v1/coords" {
            Ok(json!({"x":-123,"y":456}))
        } else if path == "/v1/help" {
            Ok(
                json!({"platform":"fixture","topic":"registry_hub","text":"Native Help: {type text}"}),
            )
        } else if path == "/v1/control" && body["run"] == "busy" {
            Err(ApiError::new(409, "Another control run is active"))
        } else {
            Ok(json!({"path":path,"no_replace":body["no_replace"]}))
        }
    });
    let key = "mcp-control-integration-key-0123456789";
    let server = Server::bind(
        Settings {
            address: ([127, 0, 0, 1], 0).into(),
            key: key.into(),
        },
        dispatch,
    )
    .unwrap();
    let mut command = Command::new(executable);
    if !native {
        command.arg("mcp");
    }
    let mut process = command
        .args(["--env", "missing-control-test.env"])
        .env("MACROHELP_CONTROL_PORT", server.address.port().to_string())
        .env("MACROHELP_CONTROL_KEY", key)
        .env("MACROHELP_CONTROL_ENABLED", "true")
        .env("MACROHELP_API_KEY", "")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let requests = [
        json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"contract-test","version":"1"}}}),
        json!({"jsonrpc":"2.0","method":"notifications/initialized"}),
        json!({"jsonrpc":"2.0","id":2,"method":"tools/list"}),
        json!({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"macrohelp_control","arguments":{"menu":2,"inputs":["Z","{type a,b}","P"],"no_replace":true}}}),
        json!({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"macrohelp_state"}}),
        json!({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"macrohelp_stop"}}),
        json!({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"macrohelp_coords"}}),
        json!({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"macrohelp_help"}}),
        json!({"jsonrpc":"2.0","id":8,"method":"resources/list"}),
        json!({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"macrohelp://guide"}}),
        json!({"jsonrpc":"2.0","id":10,"method":"resources/read","params":{"uri":"macrohelp://registry-hub"}}),
        json!({"jsonrpc":"2.0","id":11,"method":"resources/read","params":{"uri":"macrohelp://schema"}}),
        json!({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"macrohelp_control","arguments":{"menu":2,"unknown":true}}}),
        json!({"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"macrohelp_control","arguments":"2,Z,P"}}),
        json!({"jsonrpc":"2.0","id":14,"method":"tools/call","params":{"name":"does_not_exist"}}),
        json!({"jsonrpc":"2.0","id":{"bad":true},"method":"ping"}),
        json!({"jsonrpc":"2.0","id":16,"method":"tools/call","params":"not an object"}),
        json!({"jsonrpc":"2.0","id":17,"method":"tools/call","params":{"name":"macrohelp_control","arguments":{"run":"busy"}}}),
        json!({"jsonrpc":"2.0","id":18,"method":"tools/call","params":{"name":"macrohelp_help","arguments":{"menu":2}}}),
        json!({"jsonrpc":"2.0","id":19,"method":"resources/read","params":{"uri":"macrohelp://does-not-exist"}}),
    ];
    let mut input = process.stdin.take().unwrap();
    for request in requests {
        writeln!(input, "{request}").unwrap();
    }
    writeln!(input, "{{broken JSON").unwrap();
    writeln!(
        input,
        "{}",
        json!({"jsonrpc":"2.0","id":999,"method":"ping"})
    )
    .unwrap();
    drop(input);
    let result = process.wait_with_output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    assert!(result.stderr.is_empty());
    let responses = String::from_utf8(result.stdout)
        .unwrap()
        .lines()
        .map(|line| serde_json::from_str::<Value>(line).unwrap())
        .collect::<Vec<_>>();
    assert_eq!(responses.len(), 21);
    assert_eq!(responses[1]["result"]["tools"], control_mcp::tools());
    assert_eq!(
        responses[0]["result"]["instructions"],
        control_mcp::contract()["instructions"]
    );
    assert_eq!(
        responses[0]["result"]["capabilities"]["resources"]["subscribe"],
        false
    );
    let coords = responses[1]["result"]["tools"]
        .as_array()
        .unwrap()
        .iter()
        .find(|tool| tool["name"] == "macrohelp_coords")
        .unwrap();
    assert_eq!(coords["annotations"]["readOnlyHint"], true);
    assert_eq!(
        responses[2]["result"]["structuredContent"]["no_replace"],
        true
    );
    assert_eq!(
        responses[4]["result"]["structuredContent"]["path"],
        "/v1/stop"
    );
    let calls = calls.lock().unwrap();
    assert_eq!(
        responses[5]["result"]["structuredContent"],
        json!({"x":-123,"y":456})
    );
    assert_eq!(
        responses[6]["result"]["structuredContent"]["text"],
        "Native Help: {type text}"
    );
    let resources = responses[7]["result"]["resources"].as_array().unwrap();
    assert_eq!(resources.len(), 3);
    assert!(resources.iter().all(|r| r.get("route").is_none()));
    assert_eq!(
        responses[8]["result"]["contents"][0]["text"],
        include_str!("../../../../skills/macrohelp/SKILL.md")
    );
    assert_eq!(
        responses[9]["result"]["contents"][0]["text"],
        responses[6]["result"]["structuredContent"]["text"]
    );
    assert_eq!(
        serde_json::from_str::<Value>(
            responses[10]["result"]["contents"][0]["text"]
                .as_str()
                .unwrap()
        )
        .unwrap(),
        control_protocol::schema()
    );
    for i in [11, 12, 17] {
        assert_eq!(responses[i]["result"]["isError"], true);
        assert_eq!(
            responses[i]["result"]["structuredContent"]["error"]["status"],
            400
        );
        assert!(
            responses[i]["result"]["structuredContent"]["error"]["next"]
                .as_str()
                .unwrap()
                .contains("macrohelp_schema")
        );
    }
    assert_eq!(responses[13]["error"]["code"], -32602);
    assert_eq!(responses[14]["error"]["code"], -32600);
    assert_eq!(responses[14]["id"], Value::Null);
    assert_eq!(responses[15]["error"]["code"], -32602);
    assert_eq!(
        responses[16]["result"]["structuredContent"]["error"]["status"],
        409
    );
    assert!(
        responses[16]["result"]["structuredContent"]["error"]["next"]
            .as_str()
            .unwrap()
            .contains("your own active run")
    );
    assert_eq!(responses[18]["error"]["code"], -32002);
    assert_eq!(responses[19]["error"]["code"], -32700);
    assert!(responses[19]["error"]["data"]["next"].is_string());
    assert_eq!(responses[20]["id"], 999);
    assert!(responses[20]["result"].is_object());
    // Only accepted calls reach HTTP; no invalid call or recovery hint replays work.
    assert_eq!(calls.len(), 7);
    assert_eq!(calls[3].0, "GET");
    assert_eq!(calls[3].1, "/v1/coords");
    assert_eq!(calls[0].2["inputs"], json!(["Z", "{type a,b}", "P"]));
}
