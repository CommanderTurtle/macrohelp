//! The controller must prove it owns the handshake nonce before any client
//! credential crosses the wire, so a foreign UID that pre-binds the port
//! captures at most the challenge — never the persistent API key.
use macrohelp_wayland::{
    config::Config, http_server::App, task_executor::Executor, task_runner::Runner,
};
use serde_json::{Value, json};
use std::{
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    path::PathBuf,
    sync::Arc,
};
const KEY: &str = "macrohelp-local-test-key-0123456789";
const NONCE: &str = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
struct Stub;
impl Executor for Stub {
    fn call(&self, _: Value) -> macrohelp_wayland::types::Result<Value> {
        Ok(json!({"ready": true}))
    }
}
fn app(nonce: &str) -> Arc<App> {
    let config = Config {
        install: PathBuf::from("/unused"),
        socket: PathBuf::from("/unused/omarsket.sock"),
        address: ([127, 0, 0, 1], 7777).into(),
        key: KEY.into(),
        default_delay: 1,
        allow_system: false,
        cors: false,
        env_file: PathBuf::from("/unused/.env"),
        state_dir: PathBuf::from("/unused/state"),
        nonce_file: None,
    };
    Arc::new(App::new(Runner::new(config, Arc::new(Stub)), nonce.into()))
}
fn pair() -> (TcpStream, TcpStream) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let client = TcpStream::connect(listener.local_addr().unwrap()).unwrap();
    let (server, _) = listener.accept().unwrap();
    (client, server)
}
fn read_one(stream: &mut TcpStream) -> (u16, Value) {
    let mut data = Vec::new();
    let mut byte = [0u8; 1];
    while !data.ends_with(b"\r\n\r\n") {
        if stream.read(&mut byte).unwrap() == 0 {
            break;
        }
        data.push(byte[0]);
    }
    let head = String::from_utf8_lossy(&data).into_owned();
    let status: u16 = head.split_whitespace().nth(1).unwrap().parse().unwrap();
    let length = head
        .lines()
        .find_map(|l| {
            let (n, v) = l.split_once(':')?;
            if n.trim().eq_ignore_ascii_case("content-length") {
                Some(v.trim().parse::<usize>().unwrap())
            } else {
                None
            }
        })
        .unwrap_or(0);
    let mut body = vec![0u8; length];
    stream.read_exact(&mut body).unwrap();
    (
        status,
        serde_json::from_slice(&body).unwrap_or(Value::Null),
    )
}
#[test]
fn challenge_proves_identity_then_key_travels_same_connection() {
    let (mut client, server) = pair();
    let handle = app(NONCE);
    let thread = std::thread::spawn(move || handle.handle(server));
    write!(client,"GET /challenge HTTP/1.1\r\nHost: 127.0.0.1:7777\r\nConnection: keep-alive\r\n\r\n").unwrap();
    let (status, body) = read_one(&mut client);
    assert_eq!(status, 200);
    assert_eq!(body["data"]["nonce"], NONCE);
    // The challenge consumed no credential: only now does the key go out.
    write!(client,"GET /secret HTTP/1.1\r\nHost: 127.0.0.1:7777\r\nX-Tasket-Key: {KEY}\r\nConnection: close\r\n\r\n").unwrap();
    let (status, body) = read_one(&mut client);
    assert_eq!(status, 200);
    assert_eq!(body["data"]["key"], KEY);
    thread.join().unwrap();
}
#[test]
fn secret_still_requires_the_key() {
    let (mut client, server) = pair();
    let handle = app(NONCE);
    let thread = std::thread::spawn(move || handle.handle(server));
    write!(client, "GET /secret HTTP/1.1\r\nHost: 127.0.0.1:7777\r\nConnection: close\r\n\r\n").unwrap();
    let (status, _) = read_one(&mut client);
    assert_eq!(status, 401);
    thread.join().unwrap();
}
#[test]
fn handshake_disabled_when_entropy_is_absent() {
    let (mut client, server) = pair();
    let handle = app("");
    let thread = std::thread::spawn(move || handle.handle(server));
    write!(client, "GET /challenge HTTP/1.1\r\nHost: 127.0.0.1:7777\r\nConnection: close\r\n\r\n").unwrap();
    let (status, _) = read_one(&mut client);
    assert_eq!(status, 503);
    thread.join().unwrap();
}
