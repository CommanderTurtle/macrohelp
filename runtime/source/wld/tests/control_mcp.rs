//! Real MCP subprocess -> real loopback HTTP -> fake UI boundary. No input injection.
use macrohelp_overlay::control_http::{Dispatch, Server, Settings};
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
        Ok(json!({"path":path,"no_replace":body["no_replace"]}))
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
    ];
    let mut input = process.stdin.take().unwrap();
    for request in requests {
        writeln!(input, "{request}").unwrap();
    }
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
    assert_eq!(responses.len(), 5);
    assert_eq!(responses[1]["result"]["tools"].as_array().unwrap().len(), 5);
    assert_eq!(
        responses[2]["result"]["structuredContent"]["no_replace"],
        true
    );
    assert_eq!(
        responses[4]["result"]["structuredContent"]["path"],
        "/v1/stop"
    );
    let calls = calls.lock().unwrap();
    assert_eq!(calls.len(), 3);
    assert_eq!(calls[0].2["inputs"], json!(["Z", "{type a,b}", "P"]));
}
