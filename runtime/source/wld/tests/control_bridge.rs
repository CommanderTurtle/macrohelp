//! Actual helper session + HTTP + simulated QML replies. No input injection.
use macrohelp_overlay::{control_http::Settings, control_mcp};
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    net::TcpListener,
    process::{Command, Stdio},
    sync::mpsc,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
#[test]
fn session_relays_authenticated_http_and_releases_pending_calls_at_exit() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    drop(listener);
    let key = "real-session-bridge-test-key-0123456789";
    let dir = std::env::temp_dir().join(format!(
        "macrohelp-control-bridge-{}-{}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let mut child = Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
        .args(["session", "--env"])
        .arg(dir.join("missing.env"))
        .env("MACROHELP_STATE_DIR", &dir)
        .env("MACROHELP_CONTROL_KEY", key)
        .env("MACROHELP_CONTROL_PORT", address.port().to_string())
        .env("MACROHELP_CONTROL_ENABLED", "true")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    let mut input = child.stdin.take().unwrap();
    let stdout = child.stdout.take().unwrap();
    let (send, receive) = mpsc::channel();
    let reader = std::thread::spawn(move || {
        for line in BufReader::new(stdout).lines() {
            send.send(serde_json::from_str::<Value>(&line.unwrap()).unwrap())
                .unwrap();
        }
    });
    let next = || loop {
        let value = receive
            .recv_timeout(Duration::from_secs(5))
            .expect("Helper did not reply");
        if value["event"]
            .as_str()
            .is_none_or(|event| event == "control")
        {
            break value;
        }
    };
    writeln!(
        input,
        "{}",
        json!({"id":0,"method":"tracking","enabled":false})
    )
    .unwrap();
    assert_eq!(next()["id"], 0);
    writeln!(input, "{}", json!({"id":1,"method":"control-start"})).unwrap();
    let started = next();
    assert_eq!(started["id"], 1);
    assert_eq!(started["ok"], true);
    assert_eq!(started["result"]["address"], address.to_string());
    let settings = Settings {
        address,
        key: key.into(),
        cors: false,
    };
    let client = settings.clone();
    let http = std::thread::spawn(move || {
        control_mcp::http(
            &client,
            "POST",
            "/v1/control",
            &json!({"menu":2,"no_replace":true}),
        )
        .unwrap()
    });
    let event = next();
    assert_eq!(event["event"], "control");
    assert_eq!(event["body"]["no_replace"], true);
    writeln!(
        input,
        "{}",
        json!({"id":2,"method":"control-live","ticket":event["ticket"]})
    )
    .unwrap();
    assert_eq!(next()["result"], true);
    writeln!(input,"{}",json!({"id":3,"method":"control-reply","ticket":event["ticket"],"result":{"run":"owned"},"error":null})).unwrap();
    assert_eq!(next()["ok"], true);
    assert_eq!(http.join().unwrap()["run"], "owned");
    writeln!(
        input,
        "{}",
        json!({"id":4,"method":"control-live","ticket":event["ticket"]})
    )
    .unwrap();
    assert_eq!(next()["result"], false);
    let http =
        std::thread::spawn(move || control_mcp::http(&settings, "GET", "/v1/state", &json!({})));
    assert_eq!(next()["event"], "control");
    drop(input);
    assert_eq!(http.join().unwrap().unwrap_err().status, 503);
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    assert!(output.stderr.is_empty());
    reader.join().unwrap();
}
