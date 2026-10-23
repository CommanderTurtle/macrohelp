//! Route-for-route counterpart of the Qt HttpServer, using the same JSON envelope.
use crate::{
    task_registry,
    task_runner::Runner,
    types::{ApiError, Result, failure, ok, string},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};
pub const ROUTES: &[(&str, &str)] = &[
    ("GET", "/"),
    ("GET", "/health"),
    ("GET", "/tasks"),
    ("GET", "/run"),
    ("POST", "/run"),
    ("POST", "/temp-task"),
    ("GET", "/check"),
    ("GET", "/task"),
    ("GET", "/status"),
    ("POST", "/stop"),
    ("POST", "/entrypoint"),
    ("GET", "/entrypoints"),
    ("POST", "/grid"),
    ("GET", "/grid"),
];
pub struct App {
    pub runner: Runner,
    entrypoints: Mutex<BTreeMap<String, Value>>,
    grid: Mutex<BTreeMap<String, Value>>,
}
impl App {
    pub fn new(runner: Runner) -> Self {
        Self {
            runner,
            entrypoints: Mutex::new(BTreeMap::new()),
            grid: Mutex::new(BTreeMap::new()),
        }
    }
    pub fn dispatch(
        &self,
        method: &str,
        path: &str,
        query: &BTreeMap<String, String>,
        body: &Value,
    ) -> Result<Value> {
        let id = || -> Result<u64> {
            let s = query
                .get("id")
                .ok_or_else(|| ApiError::new(400, "Missing 'id' query parameter"))?;
            s.parse::<u64>()
                .ok()
                .filter(|n| *n > 0)
                .ok_or_else(|| ApiError::new(400, "Invalid task number"))
        };
        match (method, path) {
            ("GET", "/") => {
                let s = self.runner.snapshot()?;
                Ok(ok(
                    "Macrohelp Wayland HTTP Trigger Daemon",
                    json!({"name":"Macrohelp HTTP Trigger","version":env!("CARGO_PKG_VERSION"),"daemon":"macrohelp-httpd","listen_address":format!("http://{}",self.runner.config.address),"tasks_directory":format!("{}/schedules",s["directory"].as_str().unwrap_or("")),"auth_enabled":true,"default_delay_seconds":self.runner.config.default_delay,"tools":tool_inventory(),"endpoints":ROUTES.iter().map(|(m,p)|format!("{m} {p}")).collect::<Vec<_>>()}),
                ))
            }
            ("GET", "/health") => {
                let s = self.runner.snapshot()?;
                Ok(ok(
                    "Healthy",
                    json!({"status":"ok","address":format!("http://{}",self.runner.config.address),"tasks_dir":format!("{}/schedules",s["directory"].as_str().unwrap_or("")),"task_count":s["tasks"].as_array().map_or(0,Vec::len),"backend":"omarsket","shared_queue":true}),
                ))
            }
            ("GET", "/tasks") => {
                self.runner.executor.call(json!({"method":"reload"}))?;
                let s = self.runner.snapshot()?;
                let list=s["tasks"].as_array().ok_or_else(||ApiError::new(502,"Invalid task list"))?.iter().map(|t|json!({"name":t["id"],"delay_seconds":t["delay_seconds"],"description":t["description"]})).collect::<Vec<_>>();
                Ok(ok(
                    format!("Found {} task(s)", list.len()),
                    json!({"count":list.len(),"tasks":list}),
                ))
            }
            ("GET", "/run") | ("POST", "/run") => {
                let fields = if method == "GET" {
                    let mut v = json!({});
                    for key in ["task", "delay", "loop"] {
                        if let Some(x) = query.get(key) {
                            v[key] = json!(x);
                        }
                    }
                    v
                } else {
                    body.clone()
                };
                let j = self.runner.run(&fields)?;
                Ok(ok(
                    format!(
                        "'{}' scheduled to run in {}s : success",
                        j["task"].as_str().unwrap_or(""),
                        j["delay_seconds"]
                    ),
                    task_registry::task(&j, false),
                ))
            }
            ("POST", "/temp-task") => {
                let data = self.runner.temporary(body)?;
                Ok(ok(
                    if data["scheduled"] == true {
                        format!(
                            "Temporary task '{}' scheduled",
                            data["name"].as_str().unwrap_or("")
                        )
                    } else {
                        format!(
                            "Temporary task '{}' written",
                            data["name"].as_str().unwrap_or("")
                        )
                    },
                    data,
                ))
            }
            ("GET", "/check") | ("GET", "/task") => {
                let id = id()?;
                let s = self.runner.snapshot()?;
                let job = s["jobs"]
                    .as_array()
                    .and_then(|a| a.iter().find(|j| j["number"] == id))
                    .ok_or_else(|| ApiError::new(404, format!("Task #{id} not found")))?;
                Ok(ok(
                    task_registry::message(job),
                    task_registry::task(job, true),
                ))
            }
            ("GET", "/status") => {
                let s = self.runner.snapshot()?;
                Ok(ok(
                    "Daemon status",
                    task_registry::status(
                        s["jobs"]
                            .as_array()
                            .ok_or_else(|| ApiError::new(502, "Missing job states"))?,
                    ),
                ))
            }
            ("POST", "/stop") => {
                let number = if query.contains_key("id") {
                    Some(id()?)
                } else {
                    None
                };
                self.runner.stop(number)?;
                Ok(ok(
                    number.map_or_else(
                        || "All active tasks stopped".into(),
                        |id| format!("Task #{id} stopped"),
                    ),
                    Value::Null,
                ))
            }
            ("POST", "/entrypoint") | ("POST", "/grid") => {
                let key = string(body, "id")?;
                if key.len() > 120 || key.chars().any(char::is_control) {
                    return Err(ApiError::new(400, "Invalid entrypoint/grid id"));
                }
                let value = &body["value"];
                if value.is_null()
                    || value.is_array()
                    || value.is_object()
                    || value.to_string().len() > 16384
                {
                    return Err(ApiError::new(
                        400,
                        "Value must be a bounded string, boolean or number",
                    ));
                }
                let store = if path == "/grid" {
                    &self.grid
                } else {
                    &self.entrypoints
                };
                let mut store = store.lock().unwrap();
                if store.len() >= 1024 && !store.contains_key(key) {
                    return Err(ApiError::new(409, "1024 values already stored"));
                }
                store.insert(key.into(), value.clone());
                let mut result = json!({"id":key,"value":value});
                if path == "/entrypoint" {
                    result["type"] =
                        json!(body["type"].as_str().unwrap_or(if value.is_boolean() {
                            "bool"
                        } else if value.is_number() {
                            "float"
                        } else {
                            "string"
                        }));
                }
                Ok(ok(
                    format!(
                        "{} '{key}' set",
                        if path == "/grid" {
                            "Grid cell"
                        } else {
                            "Entrypoint"
                        }
                    ),
                    result,
                ))
            }
            ("GET", "/entrypoints") => {
                let s = self.entrypoints.lock().unwrap();
                Ok(ok(
                    "Entrypoint values",
                    json!({"count":s.len(),"entrypoints":*s}),
                ))
            }
            ("GET", "/grid") => {
                let s = self.grid.lock().unwrap();
                Ok(ok("Grid cell values", json!({"count":s.len(),"cells":*s})))
            }
            _ => Err(ApiError::new(404, "Not Found")),
        }
    }
    pub fn handle(&self, mut stream: TcpStream) {
        // Accepted sockets inherit nonblocking mode on Windows, unlike Linux.
        if stream.set_nonblocking(false).is_err() {
            return;
        }
        let result = (|| -> Result<Value> {
            let (method, target, headers, mut bytes) = read_headers(&mut stream)?;
            if method == "OPTIONS" && self.runner.config.cors {
                let (path,_) = split_target(&target)?;
                let requested = headers.get("access-control-request-method").map(String::as_str).unwrap_or("");
                if !ROUTES.iter().any(|(m,p)| *m == requested && *p == path) { return Err(ApiError::new(404,"Not Found")); }
                return Ok(ok("CORS preflight",json!({})));
            }
            let key = headers
                .get("x-tasket-key")
                .ok_or_else(|| ApiError::new(401, "Missing X-Tasket-Key header"))?;
            if !same_key(key.as_bytes(), self.runner.config.key.as_bytes()) {
                return Err(ApiError::new(403, "Invalid API key"));
            }
            if !self.runner.config.cors && (headers
                .get("sec-fetch-site")
                .is_some_and(|v| v == "cross-site")
                || headers.contains_key("origin"))
            {
                return Err(ApiError::new(
                    403,
                    "Browser-origin requests are not enabled",
                ));
            }
            let (path, query) = split_target(&target)?;
            if headers.contains_key("transfer-encoding") {
                return Err(ApiError::new(
                    400,
                    "Use Content-Length, not Transfer-Encoding",
                ));
            }
            let length = headers
                .get("content-length")
                .map(|v| {
                    v.parse::<usize>()
                        .map_err(|_| ApiError::new(400, "Invalid Content-Length"))
                })
                .transpose()?
                .unwrap_or(0);
            if length > 2 * 1024 * 1024 {
                return Err(ApiError::new(413, "Body exceeds 2 MiB"));
            }
            if headers.contains_key("expect") {
                return Err(ApiError::new(417, "Expect is not supported"));
            }
            let deadline = Instant::now() + Duration::from_secs(3);
            while bytes.len() < length {
                let mut chunk = [0; 8192];
                let size = chunk.len().min(length - bytes.len());
                let n = read_before(&mut stream, &mut chunk[..size], deadline)?;
                bytes.extend_from_slice(&chunk[..n]);
            }
            bytes.truncate(length);
            let body = if bytes.is_empty() {
                json!({})
            } else {
                serde_json::from_slice::<Value>(&bytes)
                    .map_err(|e| ApiError::new(400, format!("Invalid JSON: {e}")))?
            };
            if !body.is_object() {
                return Err(ApiError::new(400, "JSON body must be an object"));
            }
            self.dispatch(&method, &path, &query, &body)
        })();
        let (status, body) = match result {
            Ok(v) => (200, v),
            Err(e) => (e.status, failure(&e)),
        };
        let body = body.to_string();
        let cors = if self.runner.config.cors { "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: X-Tasket-Key, Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n" } else { "" };
        let _ = stream.set_write_timeout(Some(Duration::from_secs(3)));
        let _ = write!(
            stream,
            "HTTP/1.1 {status} {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n{cors}\r\n{body}",
            if status == 200 { "OK" } else { "Error" },
            body.len()
        );
        // Send FIN before dropping a rejected upload; an unread body must not
        // turn a complete JSON error response into a TCP reset on Windows.
        let _ = stream.shutdown(std::net::Shutdown::Write);
        let until = Instant::now() + Duration::from_millis(50);
        let mut rest = [0; 8192];
        while read_before(&mut stream, &mut rest, until).is_ok() {}
    }
}
type Head = (String, String, BTreeMap<String, String>, Vec<u8>);
fn read_before(stream: &mut TcpStream, chunk: &mut [u8], deadline: Instant) -> Result<usize> {
    let time = deadline
        .checked_duration_since(Instant::now())
        .filter(|t| !t.is_zero())
        .ok_or_else(|| ApiError::new(408, "Request timed out"))?;
    stream
        .set_read_timeout(Some(time))
        .map_err(|e| ApiError::new(400, e.to_string()))?;
    match stream.read(chunk) {
        Ok(0) => Err(ApiError::new(400, "Incomplete request")),
        Ok(n) => Ok(n),
        Err(e) => Err(ApiError::new(
            if [std::io::ErrorKind::TimedOut, std::io::ErrorKind::WouldBlock].contains(&e.kind()) {
                408
            } else {
                400
            },
            "Cannot read request",
        )),
    }
}
fn read_headers(stream: &mut TcpStream) -> Result<Head> {
    let deadline = Instant::now() + Duration::from_secs(3);
    let mut data = Vec::new();
    loop {
        let mut headers = [httparse::EMPTY_HEADER; 64];
        let mut request = httparse::Request::new(&mut headers);
        match request
            .parse(&data)
            .map_err(|e| ApiError::new(400, e.to_string()))?
        {
            httparse::Status::Complete(end) => {
                if end > 16384 {
                    return Err(ApiError::new(431, "Headers exceed 16 KiB"));
                }
                let method = request.method.unwrap_or("").to_owned();
                let target = request.path.unwrap_or("").to_owned();
                if !target.starts_with('/') || target.starts_with("//") {
                    return Err(ApiError::new(400, "Expected an origin-form request target"));
                }
                let mut fields = BTreeMap::new();
                for h in request.headers.iter() {
                    let value = std::str::from_utf8(h.value)
                        .map_err(|_| ApiError::new(400, "Invalid header text"))?;
                    if fields
                        .insert(h.name.to_ascii_lowercase(), value.to_owned())
                        .is_some()
                    {
                        return Err(ApiError::new(400, "Duplicate header"));
                    }
                }
                return Ok((method, target, fields, data[end..].to_vec()));
            }
            httparse::Status::Partial => {
                if data.len() >= 16384 {
                    return Err(ApiError::new(431, "Headers exceed 16 KiB"));
                }
                let mut chunk = [0; 4096];
                let n = read_before(stream, &mut chunk, deadline)?;
                data.extend_from_slice(&chunk[..n]);
            }
        }
    }
}
fn tool_inventory() -> Vec<Value> {
    [
        ("Run Task (GET)","Schedule a saved macro.","/run?task=<name>&delay=<sec>&loop=<n>","GET","/run?task=HelloWorld&delay=10&loop=1"),
        ("Run Task (POST)","Schedule a macro using JSON.","/run","POST",r#"{"task":"HelloWorld","delay":10,"loop":1}"#),
        ("Create Temp Task","Create or run a native/Tasket schedule. cleanup=true keeps it in memory.","/temp-task","POST",r#"{"name":"temporary","cleanup":true,"task":{"actions":[{"type":"wait","duration":1}]}}"#),
        ("Check Task","Query a task number.","/check?id=<number>","GET","/check?id=1"),
        ("Check Task Alias","Compatibility alias for check.","/task?id=<number>","GET","/task?id=1"),
        ("List Tasks","List native schedules.","/tasks","GET","/tasks"),
        ("Daemon Status","Scheduled, running and finished tasks.","/status","GET","/status"),
        ("Stop Task","Cancel a specific task.","/stop?id=<number>","POST","/stop?id=1"),
        ("Stop All","Cancel every shared-queue task.","/stop","POST","/stop"),
        ("Set Entrypoint","Set a string, boolean or number.","/entrypoint","POST",r#"{"id":"ep1","value":"hello","type":"string"}"#),
        ("List Entrypoints","List entrypoint values.","/entrypoints","GET","/entrypoints"),
        ("Set Grid Cell","Set a grid value.","/grid","POST",r#"{"id":"grid-0","value":"42"}"#),
        ("List Grid","List grid cells.","/grid","GET","/grid"),
        ("Health Check","Check controller and Omarsket connectivity.","/health","GET","/health")
    ].iter().map(|(name,description,endpoint,method,example)|json!({"name":name,"description":description,"endpoint":endpoint,"method":method,"example":example})).collect()
}
pub fn same_key(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (a, b) in a.iter().zip(b) {
        diff |= a ^ b;
    }
    diff == 0
}
pub fn split_target(target: &str) -> Result<(String, BTreeMap<String, String>)> {
    let (path, raw) = target.split_once('?').unwrap_or((target, ""));
    let mut query = BTreeMap::new();
    for pair in raw.split('&').filter(|s| !s.is_empty()) {
        let (k, v) = pair.split_once('=').unwrap_or((pair, ""));
        let k = decode(k)?;
        if query.insert(k, decode(v)?).is_some() {
            return Err(ApiError::new(400, "Duplicate query parameter"));
        }
    }
    Ok((path.into(), query))
}
fn decode(s: &str) -> Result<String> {
    let b = s.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'%' => {
                if i + 2 >= b.len() {
                    return Err(ApiError::new(400, "Invalid URL escape"));
                }
                let hex = std::str::from_utf8(&b[i + 1..i + 3])
                    .ok()
                    .and_then(|h| u8::from_str_radix(h, 16).ok())
                    .ok_or_else(|| ApiError::new(400, "Invalid URL escape"))?;
                out.push(hex);
                i += 3
            }
            b'+' => {
                out.push(b' ');
                i += 1
            }
            c => {
                out.push(c);
                i += 1
            }
        }
    }
    String::from_utf8(out).map_err(|_| ApiError::new(400, "URL must be UTF-8"))
}
pub fn serve(server: Arc<TcpListener>, app: Arc<App>, stop: Arc<AtomicBool>) {
    server
        .set_nonblocking(true)
        .expect("Set listener nonblocking");
    let workers: Vec<_> = (0..4)
        .map(|_| {
            let server = server.clone();
            let app = app.clone();
            let stop = stop.clone();
            std::thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    match server.accept() {
                        Ok((stream, _)) => app.handle(stream),
                        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            std::thread::sleep(Duration::from_millis(10))
                        }
                        Err(_) => break,
                    }
                }
            })
        })
        .collect();
    for worker in workers {
        let _ = worker.join();
    }
    app.runner.stop_owned();
}
