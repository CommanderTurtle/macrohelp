//! Legacy TCP challenge compatibility. Linux clients use peer-checked Unix sockets.
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
#[cfg(target_os = "linux")]
#[test]
fn private_controller_uses_the_existing_authenticated_routes() {
    use macrohelp_wayland::{http_server::serve, private_transport};
    use std::{os::unix::fs::PermissionsExt, sync::atomic::{AtomicBool, Ordering}, time::{Duration, Instant}};
    let dir = std::env::temp_dir().join(format!("mh-http-{}-{}",std::process::id(),std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
    std::fs::create_dir(&dir).unwrap();
    std::fs::set_permissions(&dir,std::fs::Permissions::from_mode(0o700)).unwrap();
    let nonce=dir.join("httpd.nonce");
    let server=Arc::new(TcpListener::bind("127.0.0.1:0").unwrap());
    let address=server.local_addr().unwrap();
    let mut config=app(NONCE).runner.config.clone();
    config.address=address;
    config.nonce_file=Some(nonce.clone());
    let service=Arc::new(App::new(Runner::new(config,Arc::new(Stub)),NONCE.into()));
    let stop=Arc::new(AtomicBool::new(false));
    let quitting=stop.clone();
    let thread=std::thread::spawn(move || serve(server,service,quitting).unwrap());
    let until=Instant::now()+Duration::from_secs(3);
    while !nonce.with_extension("sock").exists() && Instant::now()<until { std::thread::sleep(Duration::from_millis(5)); }
    for key in [None,Some(KEY)] {
        let mut stream=private_transport::connect(address,Some(&nonce)).unwrap();
        stream.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
        let auth=key.map(|k|format!("X-Tasket-Key: {k}\r\n")).unwrap_or_default();
        write!(stream,"GET /secret HTTP/1.1\r\nHost: {address}\r\n{auth}Connection: close\r\n\r\n").unwrap();
        let mut response=String::new();
        stream.read_to_string(&mut response).unwrap();
        assert!(response.starts_with(if key.is_some() {"HTTP/1.1 200"} else {"HTTP/1.1 401"}),"{response}");
        assert_eq!(response.contains(KEY),key.is_some());
    }
    stop.store(true,Ordering::Relaxed);
    thread.join().unwrap();
    assert!(!nonce.with_extension("sock").exists());
    std::fs::remove_dir_all(dir).unwrap();
}
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
fn legacy_challenge_and_authenticated_secret_share_one_connection() {
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
