//! The actual plugin client loads and stays quiet without an HTTP server.
use serde_json::Value;
use std::{
    io::Write,
    process::{Command, Stdio},
};

#[test]
fn missing_server_is_silent_and_local_state_remains_available() {
    for key in ["", "macrohelp-offline-test-key-0123456789"] {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);
        let temp = std::env::temp_dir().join(format!(
            "macrohelp-client-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let mut child = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
            .args(["session", "--env"])
            .arg(temp.join("missing.env"))
            .env("MACROHELP_API_KEY", key)
            .env("MACROHELP_BIND", "127.0.0.1")
            .env("MACROHELP_PORT", port.to_string())
            .env("MACROHELP_STATE_DIR", &temp)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        let input = concat!(
            "{\"id\":1,\"method\":\"hello\"}\n",
            "{\"id\":2,\"method\":\"http\",\"verb\":\"GET\",\"path\":\"/health\"}\n",
            "{\"id\":3,\"method\":\"stop\"}\n"
        );
        child
            .stdin
            .take()
            .unwrap()
            .write_all(input.as_bytes())
            .unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(output.status.success());
        assert!(
            output.stderr.is_empty(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let replies: Vec<Value> = String::from_utf8(output.stdout)
            .unwrap()
            .lines()
            .map(|line| serde_json::from_str(line).unwrap())
            .collect();
        let hello = replies.iter().find(|r| r["id"] == 1).unwrap();
        assert_eq!(hello["ok"], true);
        assert_eq!(
            hello["result"]["state"]["points"].as_array().unwrap().len(),
            16
        );
        for id in [2, 3] {
            let r = replies.iter().find(|r| r["id"] == id).unwrap();
            assert_eq!(r["offline"], true);
            assert!(r.get("error").is_none());
        }
        let stop = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
            .args(["stop", "--env"])
            .arg(temp.join("missing.env"))
            .env("MACROHELP_API_KEY", key)
            .env("MACROHELP_BIND", "127.0.0.1")
            .env("MACROHELP_PORT", port.to_string())
            .output()
            .unwrap();
        assert!(stop.status.success());
        assert!(stop.stdout.is_empty());
        assert!(stop.stderr.is_empty());
    }
}

#[test]
fn local_storage_failures_are_not_reported_as_http_offline() {
    let path = std::env::temp_dir().join(format!(
        "macrohelp-state-file-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    // A file cannot be the parent directory for overlay.json.
    std::fs::write(&path, b"not a directory").unwrap();
    let mut child = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
        .args(["session", "--env"])
        .arg(path.join("missing.env"))
        .env("MACROHELP_API_KEY", "")
        .env("MACROHELP_BIND", "127.0.0.1")
        .env("MACROHELP_PORT", "7777")
        .env("MACROHELP_STATE_DIR", &path)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let request = serde_json::json!({"id":1,"method":"save-state","state":macrohelp_overlay::overlay::default_state()});
    writeln!(child.stdin.take().unwrap(), "{request}").unwrap();
    let output = child.wait_with_output().unwrap();
    std::fs::remove_file(&path).unwrap();
    assert!(output.status.success());
    assert!(output.stderr.is_empty());
    let replies: Vec<Value> = String::from_utf8(output.stdout)
        .unwrap()
        .lines()
        .map(|line| serde_json::from_str(line).unwrap())
        .collect();
    let reply = replies.iter().find(|r| r["id"] == 1).unwrap();
    assert_eq!(reply["ok"], false);
    assert_eq!(reply["code"], 500);
    assert!(reply["error"].is_string());
    assert!(reply.get("offline").is_none());
}
