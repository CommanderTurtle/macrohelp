//! Control-plane identity: the overlay session answers a nonce challenge
//! before any client discloses the persistent Bearer key, on one connection.
use macrohelp_overlay::{control_http::Server, control_http::Settings, control_mcp};
use serde_json::{Value, json};
use std::{
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    path::PathBuf,
    sync::Arc,
};
const KEY: &str = "control-handshake-integration-key-0123456789";
fn sandbox(label: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "macrohelp-{label}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}
#[test]
fn verified_control_round_trip_over_one_connection() {
    let dir = sandbox("control-verified");
    let dispatch: macrohelp_overlay::control_http::Dispatch = Arc::new(
        |_method: &str, _path: &str, _body: &Value| -> macrohelp_overlay::client::Result<Value> {
            Ok(json!({"ready": true}))
        },
    );
    let nonce_file = dir.join("control.nonce");
    let server = Server::bind(
        Settings {
            address: ([127, 0, 0, 1], 0).into(),
            key: KEY.into(),
            cors: false,
            nonce_file: Some(nonce_file.clone()),
            nonce: None,
        },
        dispatch,
    )
    .unwrap();
    let client = Settings {
        address: server.address,
        key: KEY.into(),
        cors: false,
        nonce_file: Some(nonce_file),
        nonce: None,
    };
    let value = control_mcp::http(&client, "GET", "/v1/health", &json!({})).unwrap();
    assert_eq!(value["ready"], true);
}
#[test]
fn prebound_impostor_never_sees_the_bearer_key() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    let dir = sandbox("control-prebind");
    std::fs::write(dir.join("control.nonce"), "control-nonce-expected").unwrap();
    let settings = Settings {
        address,
        key: KEY.into(),
        cors: false,
        nonce_file: Some(dir.join("control.nonce")),
        nonce: None,
    };
    let responder = std::thread::spawn(move || {
        let (mut stream, _) = listener.accept().unwrap();
        let _ = stream.write_all(
            b"HTTP/1.1 401 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        );
        let mut all = Vec::new();
        let mut chunk = [0u8; 4096];
        while let Ok(n) = stream.read(&mut chunk) {
            if n == 0 {
                break;
            }
            all.extend_from_slice(&chunk[..n]);
        }
        all
    });
    let result = control_mcp::http(&settings, "GET", "/v1/health", &json!({}));
    assert!(
        matches!(&result, Err(e) if e.message.contains("identity unverified")),
        "expected identity error, got {result:?}"
    );
    let text = String::from_utf8_lossy(&responder.join().unwrap()).into_owned();
    assert!(text.contains("GET /v1/challenge"));
    assert!(!text.contains("Authorization"));
}
