//! Authenticated loopback transport for the overlay. Never executes macros.
use crate::client::{ApiError, Config, Result};
use crate::control_protocol::{self, LIMIT};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    fs,
    io::{Read, Write},
    net::{SocketAddr, TcpListener, TcpStream},
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc,
    },
    thread,
    time::{Duration, Instant},
};

#[derive(Clone)]
pub struct Settings {
    pub address: SocketAddr,
    pub key: String,
    pub cors: bool,
}
impl Settings {
    pub fn load(config: &Config) -> Result<Option<Self>> {
        let vars: BTreeMap<String, String> = if config.env_file.is_file() {
            dotenvy::from_path_iter(&config.env_file)
                .map_err(|_| ApiError::new(400, "Invalid overlay .env"))?
                .collect::<std::result::Result<_, _>>()
                .map_err(|_| ApiError::new(400, "Invalid overlay .env"))?
        } else {
            BTreeMap::new()
        };
        let get = |name: &str| std::env::var(name).ok().or_else(|| vars.get(name).cloned());
        if get("MACROHELP_CONTROL_ENABLED").as_deref() == Some("false") {
            return Ok(None);
        }
        let port = get("MACROHELP_CONTROL_PORT")
            .unwrap_or_else(|| "7778".into())
            .parse::<u16>()
            .ok()
            .filter(|n| *n != 0)
            .ok_or_else(|| ApiError::new(400, "Invalid overlay control port"))?;
        let key = match get("MACROHELP_CONTROL_KEY").filter(|s| !s.is_empty()) {
            Some(key) => key,
            None => private_key(config.state_dir.join("control-key"))?,
        };
        if !(24..=256).contains(&key.len()) || !key.bytes().all(|b| (33..=126).contains(&b)) {
            return Err(ApiError::new(400, "Invalid overlay control key"));
        }
        Ok(Some(Self {
            address: ([127, 0, 0, 1], port).into(),
            key,
            cors: get("MACROHELP_CORS").as_deref() == Some("true"),
        }))
    }
}
fn private_key(path: PathBuf) -> Result<String> {
    let err = |e: std::io::Error| ApiError::new(500, format!("Overlay control key: {e}"));
    if let Ok(meta) = fs::symlink_metadata(&path) {
        if !meta.is_file() || meta.len() > 256 {
            return Err(ApiError::new(
                400,
                "Control key must be a regular private file",
            ));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            if meta.uid() != unsafe { libc::geteuid() } || meta.mode() & 0o077 != 0 {
                return Err(ApiError::new(
                    400,
                    "Control key must be private (chmod 600)",
                ));
            }
        }
        return fs::read_to_string(path)
            .map(|s| s.trim().into())
            .map_err(err);
    }
    let parent = path
        .parent()
        .ok_or_else(|| ApiError::new(400, "Missing control data directory"))?;
    fs::create_dir_all(parent).map_err(err)?;
    let mut random = [0u8; 32];
    #[cfg(unix)]
    {
        fs::File::open("/dev/urandom")
            .and_then(|mut file| file.read_exact(&mut random))
            .map_err(err)?;
    }
    #[cfg(windows)]
    {
        #[link(name = "bcrypt")]
        unsafe extern "system" {
            fn BCryptGenRandom(
                algorithm: *mut std::ffi::c_void,
                buffer: *mut u8,
                length: u32,
                flags: u32,
            ) -> i32;
        }
        if unsafe { BCryptGenRandom(std::ptr::null_mut(), random.as_mut_ptr(), 32, 2) } != 0 {
            return Err(ApiError::new(500, "Cannot generate overlay control key"));
        }
    }
    let key = random
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect::<String>();
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
        options.mode(0o600);
        fs::set_permissions(parent, fs::Permissions::from_mode(0o700)).map_err(err)?;
    }
    match options.open(&path) {
        Ok(mut file) => {
            file.write_all(key.as_bytes())
                .and_then(|_| file.sync_all())
                .map_err(err)?;
            Ok(key)
        }
        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => private_key(path),
        Err(e) => Err(err(e)),
    }
}

pub type Dispatch = Arc<dyn Fn(&str, &str, &Value) -> Result<Value> + Send + Sync>;
pub fn envelope(result: Result<Value>) -> (u16, Value) {
    match result {
        Ok(value) => (200, json!({"ok":true,"result":value})),
        Err(error) => (
            error.status,
            json!({"ok":false,"error":{"status":error.status,"message":error.message}}),
        ),
    }
}
fn same_key(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0, |acc, (a, b)| acc | (a ^ b)) == 0
}
fn request(stream: &mut TcpStream, settings: &Settings, dispatch: &Dispatch) -> Result<Value> {
    let deadline = Instant::now() + Duration::from_secs(5);
    stream
        .set_write_timeout(Some(Duration::from_secs(5)))
        .map_err(io)?;
    let mut bytes = Vec::new();
    let mut chunk = [0u8; 4096];
    let end = loop {
        if let Some(at) = bytes.windows(4).position(|s| s == b"\r\n\r\n") {
            break at + 4;
        }
        if bytes.len() > 16384 {
            return Err(ApiError::new(431, "Headers exceed 16 KiB"));
        }
        let remaining = deadline
            .checked_duration_since(Instant::now())
            .ok_or_else(|| ApiError::new(408, "Request timeout"))?;
        stream.set_read_timeout(Some(remaining)).map_err(io)?;
        let n = stream
            .read(&mut chunk)
            .map_err(|_| ApiError::new(408, "Request timeout"))?;
        if n == 0 {
            return Err(ApiError::new(400, "Incomplete request"));
        }
        bytes.extend_from_slice(&chunk[..n]);
    };
    if end > 16384 {
        return Err(ApiError::new(431, "Headers exceed 16 KiB"));
    }
    let head = std::str::from_utf8(&bytes[..end])
        .map_err(|_| ApiError::new(400, "Invalid request headers"))?;
    let mut lines = head.split("\r\n");
    let first = lines
        .next()
        .unwrap_or("")
        .split_whitespace()
        .collect::<Vec<_>>();
    if first.len() != 3 || !matches!(first[2], "HTTP/1.1" | "HTTP/1.0") {
        return Err(ApiError::new(400, "Invalid request line"));
    }
    let (method, path) = (first[0].to_owned(), first[1].to_owned());
    let mut headers = BTreeMap::new();
    for line in lines.filter(|line| !line.is_empty()) {
        let (name, value) = line
            .split_once(':')
            .ok_or_else(|| ApiError::new(400, "Invalid header"))?;
        if name.is_empty()
            || !name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
            || headers
                .insert(name.to_ascii_lowercase(), value.trim().to_owned())
                .is_some()
        {
            return Err(ApiError::new(400, "Invalid or duplicate header"));
        }
    }
    if headers.contains_key("transfer-encoding") {
        return Err(ApiError::new(
            400,
            "Use Content-Length, not Transfer-Encoding",
        ));
    }
    let host = headers.get("host").map(String::as_str).unwrap_or("");
    if ![
        format!("127.0.0.1:{}", settings.address.port()),
        format!("localhost:{}", settings.address.port()),
    ]
    .contains(&host.to_ascii_lowercase())
    {
        return Err(ApiError::new(403, "Loopback Host required"));
    }
    if method == "OPTIONS" && settings.cors {
        let requested = headers.get("access-control-request-method").map(String::as_str).unwrap_or("");
        let allowed = (requested == "GET" && ["/v1/health","/v1/schema","/v1/state","/v1/coords","/v1/board","/v1/help","/v1/skills"].contains(&path.as_str()))
            || (requested == "POST" && ["/v1/control","/v1/stop","/v1/skills","/v1/skills/query"].contains(&path.as_str()));
        if !allowed { return Err(ApiError::new(404,"Unknown overlay control route")); }
        return Ok(json!({"cors":true}));
    }
    let expected = format!("Bearer {}", settings.key);
    if !headers.get("authorization").is_some_and(|v| same_key(v.as_bytes(), expected.as_bytes())) {
        return Err(ApiError::new(401, "Overlay control key required"));
    }
    if !matches!(
        (method.as_str(), path.as_str()),
        (
            "GET",
            "/v1/health" | "/v1/schema" | "/v1/state" | "/v1/coords" | "/v1/board" | "/v1/help" | "/v1/skills"
        ) | ("POST", "/v1/control" | "/v1/stop" | "/v1/skills" | "/v1/skills/query")
    ) {
        return Err(ApiError::new(404, "Unknown overlay control route"));
    }
    let length = headers
        .get("content-length")
        .map(|v| {
            v.parse::<usize>()
                .map_err(|_| ApiError::new(400, "Invalid Content-Length"))
        })
        .transpose()?
        .unwrap_or(0);
    if length > LIMIT {
        return Err(ApiError::new(413, "Body exceeds 1 MiB"));
    }
    if method == "POST"
        && !headers.get("content-type").is_some_and(|v| {
            v.split(';')
                .next()
                .unwrap_or("")
                .trim()
                .eq_ignore_ascii_case("application/json")
        })
    {
        return Err(ApiError::new(415, "Use application/json"));
    }
    while bytes.len() < end + length {
        let remaining = deadline
            .checked_duration_since(Instant::now())
            .ok_or_else(|| ApiError::new(408, "Request timeout"))?;
        stream.set_read_timeout(Some(remaining)).map_err(io)?;
        let wanted = (end + length - bytes.len()).min(chunk.len());
        let n = stream
            .read(&mut chunk[..wanted])
            .map_err(|_| ApiError::new(408, "Request timeout"))?;
        if n == 0 {
            return Err(ApiError::new(400, "Incomplete body"));
        }
        bytes.extend_from_slice(&chunk[..n]);
    }
    let body = if length == 0 {
        json!({})
    } else {
        serde_json::from_slice(&bytes[end..end + length])
            .map_err(|_| ApiError::new(400, "Invalid UTF-8 JSON body"))?
    };
    if path == "/v1/schema" {
        return Ok(control_protocol::schema());
    }
    let body = if path == "/v1/control" {
        control_protocol::normalize(&body)?
    } else if method == "POST" && path.starts_with("/v1/skills") {
        crate::control_skills::request(&body,path.ends_with("/query"))?
    } else {
        body
    };
    dispatch(&method, &path, &body)
}
fn io(error: std::io::Error) -> ApiError {
    ApiError::new(503, error.to_string())
}
pub fn handle(mut stream: TcpStream, settings: &Settings, dispatch: &Dispatch) {
    // Windows accepted sockets can inherit the listener's nonblocking mode.
    if stream.set_nonblocking(false).is_err() {
        return;
    }
    let (status, body) = envelope(request(&mut stream, settings, dispatch));
    let body = body.to_string();
    let reason = if status == 200 { "OK" } else { "Error" };
    let cors = if settings.cors { "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Authorization, Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n" } else { "" };
    let _ = write!(
        stream,
        "HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n{cors}\r\n{body}",
        body.len()
    );
}
pub struct Server {
    pub address: SocketAddr,
    stop: Arc<AtomicBool>,
    worker: Option<thread::JoinHandle<()>>,
}
impl Server {
    pub fn bind(mut settings: Settings, dispatch: Dispatch) -> Result<Self> {
        let listener = Arc::new(TcpListener::bind(settings.address).map_err(io)?);
        listener.set_nonblocking(true).map_err(io)?;
        settings.address = listener.local_addr().map_err(io)?;
        let address = settings.address;
        let stop = Arc::new(AtomicBool::new(false));
        let quitting = stop.clone();
        let worker = thread::spawn(move || {
            let workers = (0..4)
                .map(|_| {
                    let listener = listener.clone();
                    let stop = quitting.clone();
                    let dispatch = dispatch.clone();
                    let settings = settings.clone();
                    thread::spawn(move || {
                        while !stop.load(Ordering::Relaxed) {
                            match listener.accept() {
                                Ok((stream, _)) => handle(stream, &settings, &dispatch),
                                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                    thread::sleep(Duration::from_millis(10))
                                }
                                Err(_) => break,
                            }
                        }
                    })
                })
                .collect::<Vec<_>>();
            for worker in workers {
                let _ = worker.join();
            }
        });
        Ok(Self {
            address,
            stop,
            worker: Some(worker),
        })
    }
}
impl Drop for Server {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

#[derive(Default)]
pub struct Bridge {
    next: AtomicU64,
    closed: AtomicBool,
    pending: Mutex<BTreeMap<u64, mpsc::SyncSender<Result<Value>>>>,
}
impl Bridge {
    pub fn dispatch(self: &Arc<Self>) -> Dispatch {
        let bridge = self.clone();
        Arc::new(move |method, path, body| {
            let id = bridge.next.fetch_add(1, Ordering::Relaxed) + 1;
            let (send, receive) = mpsc::sync_channel(1);
            {
                let mut pending = bridge.pending.lock().unwrap();
                if bridge.closed.load(Ordering::Relaxed) {
                    return Err(ApiError::new(503, "Overlay stopped"));
                }
                if pending.len() >= 8 {
                    return Err(ApiError::new(429, "Overlay control queue is full"));
                }
                pending.insert(id, send);
            }
            let expires = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis()
                + 15000;
            println!(
                "{}",
                json!({"event":"control","ticket":id,"expires":expires,"method":method,"path":path,"body":body})
            );
            let _ = std::io::stdout().flush();
            let result = receive
                .recv_timeout(Duration::from_secs(15))
                .unwrap_or_else(|_| {
                    Err(ApiError::new(504, "Overlay did not answer control request"))
                });
            bridge.pending.lock().unwrap().remove(&id);
            result
        })
    }
    pub fn reply(&self, value: &Value) -> Result<Value> {
        let id = value["ticket"]
            .as_u64()
            .ok_or_else(|| ApiError::new(400, "Missing control ticket"))?;
        if let Some(send) = self.pending.lock().unwrap().remove(&id) {
            let result = if let Some(error) = value.get("error").filter(|v| !v.is_null()) {
                Err(ApiError::new(
                    error["status"].as_u64().unwrap_or(400) as u16,
                    error["message"]
                        .as_str()
                        .unwrap_or("Overlay control failed"),
                ))
            } else {
                Ok(value["result"].clone())
            };
            let _ = send.send(result);
        }
        Ok(json!(true))
    }
    pub fn live(&self, ticket: u64) -> bool {
        self.pending.lock().unwrap().contains_key(&ticket)
    }
    pub fn close(&self) {
        self.closed.store(true, Ordering::Relaxed);
        for (_, send) in std::mem::take(&mut *self.pending.lock().unwrap()) {
            let _ = send.send(Err(ApiError::new(503, "Overlay stopped")));
        }
    }
}
