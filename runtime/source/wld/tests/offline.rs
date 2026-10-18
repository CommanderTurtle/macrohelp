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

#[test]
fn slow_http_does_not_block_the_local_overlay() {
    use std::io::{BufRead, BufReader, Read};
    use std::sync::mpsc;
    use std::time::Duration;
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let (release, wait) = mpsc::channel();
    let server = std::thread::spawn(move || {
        let (mut socket, _) = listener.accept().unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        let mut input = BufReader::new(&socket);
        let mut length = 0;
        loop {
            let mut line = String::new();
            input.read_line(&mut line).unwrap();
            if line == "\r\n" {
                break;
            }
            if let Some(value) = line.strip_prefix("Content-Length: ") {
                length = value.trim().parse::<usize>().unwrap();
            }
        }
        input.read_exact(&mut vec![0; length]).unwrap();
        drop(input);
        let _ = wait.recv_timeout(Duration::from_secs(3));
        let body = r#"{"success":true,"data":{"status":"ok"}}"#;
        write!(
            socket,
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
            body.len(),
            body
        )
        .unwrap();
    });
    let mut child = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
        .args(["session", "--env", "missing.env"])
        .env("MACROHELP_API_KEY", "macrohelp-local-client-test-key")
        .env("MACROHELP_BIND", "127.0.0.1")
        .env("MACROHELP_PORT", port.to_string())
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut input = child.stdin.take().unwrap();
    writeln!(input,"{{\"id\":1,\"method\":\"http\",\"verb\":\"GET\",\"path\":\"/health\"}}\n{{\"id\":2,\"method\":\"hello\"}}").unwrap();
    let output = child.stdout.take().unwrap();
    let (send, receive) = mpsc::channel();
    let reader = std::thread::spawn(move || {
        for line in BufReader::new(output).lines() {
            if send.send(line.unwrap()).is_err() {
                break;
            }
        }
    });
    // The held HTTP response cannot be released until local hello is answered.
    let first: Value =
        serde_json::from_str(&receive.recv_timeout(Duration::from_secs(2)).unwrap()).unwrap();
    assert_eq!(first["id"], 2);
    release.send(()).unwrap();
    let second: Value =
        serde_json::from_str(&receive.recv_timeout(Duration::from_secs(2)).unwrap()).unwrap();
    assert_eq!(second["id"], 1);
    assert_eq!(second["ok"], true);
    drop(input);
    assert!(child.wait().unwrap().success());
    server.join().unwrap();
    reader.join().unwrap();
}

#[test]
fn heatmap_snapshot_is_read_only_and_sessions_resume_after_restart() {
    let temp = std::env::temp_dir().join(format!(
        "macrohelp-heatmap-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let run = |input: &str| {
        let mut child = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
            .args(["session", "--env"])
            .arg(temp.join("missing.env"))
            .env("MACROHELP_STATE_DIR", &temp)
            .env("MACROHELP_API_KEY", "")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(input.as_bytes())
            .unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(output.status.success());
        assert!(output.stderr.is_empty());
        String::from_utf8(output.stdout)
            .unwrap()
            .lines()
            .map(|s| serde_json::from_str::<Value>(s).unwrap())
            .collect::<Vec<_>>()
    };
    run("{\"id\":1,\"method\":\"heatmap\",\"action\":\"snapshot\"}\n");
    assert!(!temp.join("heatmap.json").exists());
    run(
        "{\"id\":2,\"method\":\"heatmap\",\"action\":\"start\",\"name\":\"Writing\",\"work\":1500,\"rest\":300}\n",
    );
    let replies = run(
        "{\"id\":3,\"method\":\"heatmap\",\"action\":\"snapshot\"}\n{\"id\":4,\"method\":\"heatmap\",\"action\":\"end\"}\n",
    );
    let snapshot = &replies.iter().find(|r| r["id"] == 3).unwrap()["result"];
    assert_eq!(snapshot["sessions"][0]["name"], "Writing");
    assert!(!snapshot["active"].is_null());
    let saved = std::fs::read_to_string(temp.join("heatmap.json")).unwrap();
    assert!(serde_json::from_str::<Value>(&saved).unwrap()["active"].is_null());
    assert!(!saved.contains("events"));
    assert!(!saved.contains("held"));
    std::fs::remove_file(temp.join("heatmap.json")).unwrap();
    std::fs::remove_dir(&temp).unwrap();
}
