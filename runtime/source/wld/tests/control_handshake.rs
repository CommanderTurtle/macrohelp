//! Linux clients never disclose credentials to a TCP port, even with a replayed nonce.
#![cfg(target_os = "linux")]
use macrohelp_overlay::{
    client::Config,
    control_http::{Dispatch, Server, Settings},
    control_mcp, overlay,
    private_transport::{self, PrivateListener},
};
use serde_json::json;
use std::{
    io::{Read, Write},
    net::TcpListener,
    os::unix::fs::{MetadataExt, PermissionsExt},
    path::PathBuf,
    sync::Arc,
    time::{Duration, Instant},
};

const KEY: &str = "transport-canary-key-0123456789";
fn sandbox(label: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "mh-{label}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::create_dir(&dir).unwrap();
    std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o700)).unwrap();
    dir
}
fn settings(address: std::net::SocketAddr, nonce: PathBuf) -> Settings {
    Settings {
        address,
        key: KEY.into(),
        cors: false,
        nonce_file: Some(nonce),
        nonce: None,
    }
}
fn controller(settings: &Settings) -> Config {
    Config {
        address: settings.address,
        key: settings.key.clone(),
        env_file: PathBuf::new(),
        state_dir: settings
            .nonce_file
            .as_ref()
            .unwrap()
            .parent()
            .unwrap()
            .into(),
        nonce_file: settings.nonce_file.clone(),
    }
}
fn both_clients_refuse(settings: &Settings) {
    assert!(
        overlay::http(&controller(settings), "GET", "/health", &json!({}))
            .unwrap_err()
            .message
            .contains("key withheld")
    );
    let mut session = control_mcp::Session::default();
    let reply = session
        .dispatch(
            &json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}),
            &|m, p, b| control_mcp::http(settings, m, p, b),
        )
        .unwrap();
    assert!(
        reply["result"]["instructions"]
            .as_str()
            .unwrap()
            .contains("unavailable"),
        "{reply}"
    );
}
#[test]
fn legitimate_control_works_and_socket_cleanup_is_owned() {
    let dir = sandbox("valid");
    let dispatch: Dispatch = Arc::new(|_, _, _| Ok(json!({"ready":true})));
    let nonce = dir.join("control.nonce");
    let server = Server::bind(
        settings("127.0.0.1:0".parse().unwrap(), nonce.clone()),
        dispatch,
    )
    .unwrap();
    let client = settings(server.address, nonce.clone());
    assert_eq!(
        control_mcp::http(&client, "GET", "/v1/health", &json!({})).unwrap()["ready"],
        true
    );
    assert_eq!(
        std::fs::metadata(nonce.with_extension("sock"))
            .unwrap()
            .mode()
            & 0o777,
        0o600
    );
    drop(server);
    assert!(!nonce.with_extension("sock").exists());
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn public_nonce_replay_missing_nonce_and_unconfigured_transport_never_touch_tcp() {
    let dir = sandbox("replay");
    let port = TcpListener::bind("127.0.0.1:0").unwrap();
    port.set_nonblocking(true).unwrap();
    let mut client = settings(port.local_addr().unwrap(), dir.join("httpd.nonce"));
    for nonce in [None, Some(""), Some("previously-observed-public-nonce")] {
        if let Some(nonce) = nonce {
            std::fs::write(client.nonce_file.as_ref().unwrap(), nonce).unwrap();
        }
        both_clients_refuse(&client);
        assert!(matches!(port.accept(),Err(e) if e.kind()==std::io::ErrorKind::WouldBlock));
    }
    client.nonce_file = None;
    assert!(control_mcp::http(&client, "GET", "/v1/health", &json!({})).is_err());
    assert!(
        overlay::http(
            &Config {
                nonce_file: None,
                ..controller(&settings(port.local_addr().unwrap(), dir.join("x")))
            },
            "GET",
            "/health",
            &json!({})
        )
        .is_err()
    );
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn unsafe_paths_are_refused_and_stale_sockets_can_restart() {
    let dir = sandbox("paths");
    let nonce = dir.join("control.nonce");
    let path = nonce.with_extension("sock");
    std::fs::write(&path, "not a socket").unwrap();
    assert!(PrivateListener::bind(&nonce).is_err());
    assert_eq!(std::fs::read(&path).unwrap(), b"not a socket");
    std::fs::remove_file(&path).unwrap();
    std::os::unix::fs::symlink("/tmp", &path).unwrap();
    assert!(PrivateListener::bind(&nonce).is_err());
    std::fs::remove_file(&path).unwrap();
    drop(std::os::unix::net::UnixListener::bind(&path).unwrap());
    let listener = PrivateListener::bind(&nonce).unwrap();
    assert!(PrivateListener::bind(&nonce).is_err());
    assert!(private_transport::connect("127.0.0.1:1".parse().unwrap(), Some(&nonce)).is_ok());
    std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o755)).unwrap();
    assert!(private_transport::connect("127.0.0.1:1".parse().unwrap(), Some(&nonce)).is_err());
    std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o700)).unwrap();
    std::fs::remove_file(&path).unwrap();
    let successor = PrivateListener::bind(&nonce).unwrap();
    drop(listener);
    assert!(path.exists(), "predecessor must not unlink its successor");
    drop(successor);
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
fn controller_receives_key_over_the_verified_socket_only() {
    let dir = sandbox("controller");
    let client = settings("127.0.0.1:1".parse().unwrap(), dir.join("httpd.nonce"));
    let listener = PrivateListener::bind(client.nonce_file.as_ref().unwrap()).unwrap();
    let (seen, receive) = std::sync::mpsc::channel();
    let (release, wait) = std::sync::mpsc::channel();
    let thread = std::thread::spawn(move || {
        let until = Instant::now() + Duration::from_secs(3);
        let mut stream = loop {
            match listener.accept() {
                Ok(s) => break s,
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock && Instant::now() < until => {
                    std::thread::sleep(Duration::from_millis(5))
                }
                Err(e) => panic!("{e}"),
            }
        };
        stream
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        let mut head = Vec::new();
        let mut byte = [0u8; 1];
        while !head.ends_with(b"\r\n\r\n") {
            stream.read_exact(&mut byte).unwrap();
            head.push(byte[0]);
        }
        assert!(String::from_utf8_lossy(&head).contains(&format!("X-Tasket-Key: {KEY}")));
        stream.read_exact(&mut [0u8; 2]).unwrap();
        seen.send(()).unwrap();
        wait.recv_timeout(Duration::from_secs(3)).unwrap();
        let body = r#"{"success":true,"data":{"ok":true}}"#;
        write!(
            stream,
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
            body.len()
        )
        .unwrap();
    });
    let child = std::process::Command::new(env!("CARGO_BIN_EXE_macrohelpctl"))
        .args(["controller-request", "--env"])
        .arg(dir.join("missing.env"))
        .env("MACROHELP_API_KEY", KEY)
        .env("MACROHELP_NONCE", client.nonce_file.as_ref().unwrap())
        .env("MACROHELP_BIND", "127.0.0.1")
        .env("MACROHELP_PORT", "1")
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    receive.recv_timeout(Duration::from_secs(3)).unwrap();
    let argv = std::fs::read(format!("/proc/{}/cmdline", child.id())).unwrap();
    assert!(!String::from_utf8_lossy(&argv).contains(KEY));
    release.send(()).unwrap();
    let response = child.wait_with_output().unwrap();
    assert!(
        response.status.success(),
        "{}",
        String::from_utf8_lossy(&response.stderr)
    );
    assert_eq!(
        serde_json::from_slice::<serde_json::Value>(&response.stdout).unwrap()["ok"],
        true
    );
    thread.join().unwrap();
    std::fs::remove_dir_all(dir).unwrap();
}

// Run with sufficient privileges to exercise a real second UID without adding users.
#[test]
fn foreign_uid_ports_and_kernel_peer_credentials_withhold_both_keys() {
    use std::os::unix::process::CommandExt;
    if unsafe { libc::geteuid() } != 0 {
        return;
    }
    let dir = sandbox("uid");
    std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o777)).unwrap();
    let executable = dir.join("fixture");
    std::fs::copy(std::env::current_exe().unwrap(), &executable).unwrap();
    std::fs::set_permissions(&executable, std::fs::Permissions::from_mode(0o755)).unwrap();
    let mut child = std::process::Command::new(executable)
        .args(["--exact", "foreign_uid_prebind_fixture", "--ignored"])
        .env_clear()
        .env("MH_FIXTURE", &dir)
        .uid(65534)
        .gid(65534)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .unwrap();
    let until = Instant::now() + Duration::from_secs(5);
    while !dir.join("ready").exists() && Instant::now() < until {
        std::thread::sleep(Duration::from_millis(10));
    }
    let ready = std::fs::read_to_string(dir.join("ready")).expect("second-UID listener startup");
    let addresses: Vec<_> = ready.lines().map(|line| line.parse().unwrap()).collect();
    assert_eq!(addresses.len(), 2);
    for (name, address) in ["httpd", "control"].into_iter().zip(addresses) {
        let client = settings(address, dir.join(format!("{name}.nonce")));
        std::fs::write(client.nonce_file.as_ref().unwrap(), "public-replayed-nonce").unwrap();
        both_clients_refuse(&client);
        // Make path metadata look legitimate; SO_PEERCRED must still reject the foreign server.
        let path = client.nonce_file.as_ref().unwrap().with_extension("sock");
        let path_c =
            std::ffi::CString::new(std::os::unix::ffi::OsStrExt::as_bytes(path.as_os_str()))
                .unwrap();
        assert_eq!(unsafe { libc::chown(path_c.as_ptr(), 0, 0) }, 0);
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).unwrap();
        std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o700)).unwrap();
        both_clients_refuse(&client);
        std::fs::set_permissions(&dir, std::fs::Permissions::from_mode(0o777)).unwrap();
    }
    std::fs::write(dir.join("stop"), "").unwrap();
    assert!(child.wait().unwrap().success());
    let captured = std::fs::read_to_string(dir.join("captured")).unwrap();
    assert!(!captured.contains(KEY), "{captured}");
    assert!(!captured.contains("Authorization"));
    assert!(!captured.contains("X-Tasket-Key"));
    std::fs::remove_dir_all(dir).unwrap();
}
#[test]
#[ignore]
fn foreign_uid_prebind_fixture() {
    let dir = PathBuf::from(std::env::var_os("MH_FIXTURE").expect("fixture-only"));
    let mut tcp = Vec::new();
    let mut unix = Vec::new();
    for name in ["httpd", "control"] {
        let port = TcpListener::bind("127.0.0.1:0").unwrap();
        port.set_nonblocking(true).unwrap();
        tcp.push(port);
        let sock =
            std::os::unix::net::UnixListener::bind(dir.join(format!("{name}.sock"))).unwrap();
        sock.set_nonblocking(true).unwrap();
        unix.push(sock);
    }
    let ready = tcp
        .iter()
        .map(|p| p.local_addr().unwrap().to_string())
        .collect::<Vec<_>>()
        .join("\n");
    std::fs::write(dir.join("ready.tmp"), ready).unwrap();
    std::fs::rename(dir.join("ready.tmp"), dir.join("ready")).unwrap();
    let mut captured = Vec::new();
    let until = Instant::now() + Duration::from_secs(8);
    while !dir.join("stop").exists() && Instant::now() < until {
        for port in &tcp {
            if let Ok((mut stream, _)) = port.accept() {
                stream
                    .set_read_timeout(Some(Duration::from_millis(100)))
                    .unwrap();
                let mut bytes = [0; 4096];
                if let Ok(n) = stream.read(&mut bytes) {
                    captured.extend_from_slice(&bytes[..n]);
                }
            }
        }
        for sock in &unix {
            if let Ok((mut stream, _)) = sock.accept() {
                stream
                    .set_read_timeout(Some(Duration::from_millis(100)))
                    .unwrap();
                let mut bytes = [0; 4096];
                if let Ok(n) = stream.read(&mut bytes) {
                    captured.extend_from_slice(&bytes[..n]);
                }
            }
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    std::fs::write(dir.join("captured"), captured).unwrap();
}
