//! Real HTTP requests backed by an actual Omarsket --dry-run process, never input.
use macrohelp_wayland::{
    config::Config,
    http_server::{App, ROUTES, serve, split_target},
    task_executor::{Executor, decode},
    task_runner::Runner,
    types::{ApiError, Result},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeSet,
    io::{BufRead, BufReader, Read, Write},
    net::{SocketAddr, TcpStream},
    path::PathBuf,
    process::{Child, ChildStdout, Command, Stdio},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};
const KEY: &str = "macrohelp-local-test-key-0123456789";
struct Engine {
    child: Mutex<(Child, BufReader<ChildStdout>, u64)>,
}
impl Engine {
    fn new(dir: &std::path::Path) -> Self {
        let bin = std::env::var_os("OMARSKET_TEST_BIN").expect(
            "Set OMARSKET_TEST_BIN to the existing Omarsket executable; tests only use --dry-run",
        );
        let mut child = Command::new(bin)
            .args(["session", "--dry-run", "--directory"])
            .arg(dir)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .unwrap();
        let mut output = BufReader::new(child.stdout.take().unwrap());
        let mut line = String::new();
        output.read_line(&mut line).unwrap();
        assert_eq!(
            serde_json::from_str::<Value>(&line).unwrap()["event"],
            "ready"
        );
        Self {
            child: Mutex::new((child, output, 0)),
        }
    }
}
impl Executor for Engine {
    fn call(&self, mut request: Value) -> Result<Value> {
        let mut c = self.child.lock().unwrap();
        c.2 += 1;
        let id = c.2;
        request["id"] = json!(id);
        writeln!(c.0.stdin.as_mut().unwrap(), "{request}")
            .map_err(|e| ApiError::new(503, e.to_string()))?;
        c.0.stdin.as_mut().unwrap().flush().unwrap();
        loop {
            let mut line = String::new();
            if c.1.read_line(&mut line).unwrap() == 0 {
                return Err(ApiError::new(503, "Engine stopped"));
            }
            let reply: Value = serde_json::from_str(&line).unwrap();
            if reply["id"] == id {
                return decode(reply);
            }
        }
    }
}
impl Drop for Engine {
    fn drop(&mut self) {
        let _ = self.call(json!({"method":"shutdown"}));
        let _ = self.child.lock().unwrap().0.wait();
    }
}
struct Harness {
    addr: SocketAddr,
    stop: Arc<AtomicBool>,
    thread: Option<std::thread::JoinHandle<()>>,
    dir: PathBuf,
}
impl Harness {
    fn new(allow_system: bool) -> Self {
        Self::with_cors(allow_system,false)
    }
    fn with_cors(allow_system: bool, cors: bool) -> Self {
        let dir = std::env::temp_dir().join(format!(
            "macrohelp-http-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        let server = Arc::new(std::net::TcpListener::bind("127.0.0.1:0").unwrap());
        let addr = server.local_addr().unwrap();
        let config = Config {
            install: PathBuf::new(),
            socket: dir.join("control.sock"),
            address: addr,
            key: KEY.into(),
            default_delay: 0,
            allow_system,
            cors,
            env_file: dir.join(".env"),
            state_dir: dir.join("state"),
        };
        let app = Arc::new(App::new(Runner::new(config, Arc::new(Engine::new(&dir)))));
        let stop = Arc::new(AtomicBool::new(false));
        let stopped = stop.clone();
        let thread = std::thread::spawn(move || serve(server, app, stopped));
        Self {
            addr,
            stop,
            thread: Some(thread),
            dir,
        }
    }
    fn req(&self, method: &str, path: &str, body: Value) -> (u16, Value) {
        self.request(method, path, body, Some(KEY), "")
    }
    fn request(
        &self,
        method: &str,
        path: &str,
        body: Value,
        key: Option<&str>,
        extra: &str,
    ) -> (u16, Value) {
        let mut stream = TcpStream::connect(self.addr).unwrap();
        stream
            .set_read_timeout(Some(Duration::from_secs(5)))
            .unwrap();
        let body = body.to_string();
        write!(stream,"{method} {path} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nContent-Type: application/json\r\n{}{}Content-Length: {}\r\n\r\n{body}",self.addr,key.map(|k|format!("X-Tasket-Key: {k}\r\n")).unwrap_or_default(),extra,body.len()).unwrap();
        let mut output = String::new();
        stream.read_to_string(&mut output).unwrap();
        let (head, body) = output.split_once("\r\n\r\n").unwrap();
        let status = head.split_whitespace().nth(1).unwrap().parse().unwrap();
        (status, serde_json::from_str(body).unwrap())
    }
    fn ok(&self, method: &str, path: &str, body: Value) -> Value {
        let (status, value) = self.req(method, path, body);
        assert_eq!(status, 200, "{value}");
        assert_eq!(value["success"], true);
        value["data"].clone()
    }
}
impl Drop for Harness {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        self.thread.take().unwrap().join().unwrap();
        std::fs::remove_dir_all(&self.dir).unwrap();
    }
}

#[test]
fn qt_route_inventory_matches_exactly() {
    let source = include_str!("../../src/HttpServer.cpp");
    let mut expected = BTreeSet::new();
    for line in source.lines() {
        for (token, method) in [("m_server->Get(\"", "GET"), ("m_server->Post(\"", "POST")] {
            if let Some((_, rest)) = line.split_once(token) {
                expected.insert((
                    method.to_string(),
                    rest.split('"').next().unwrap().to_string(),
                ));
            }
        }
    }
    let actual: BTreeSet<_> = ROUTES
        .iter()
        .map(|(m, p)| (m.to_string(), p.to_string()))
        .collect();
    assert_eq!(actual, expected);
}

#[test]
fn browser_cors_is_opt_in_and_does_not_remove_task_auth() {
    for cors in [false,true] {
        let h=Harness::with_cors(false,cors);
        let origin="Origin: https://any-site.example\r\nAccess-Control-Request-Method: GET\r\n";
        assert_eq!(h.request("OPTIONS","/tasks",json!({}),None,origin).0,if cors{200}else{401});
        assert_eq!(h.request("GET","/tasks",json!({}),None,origin).0,401);
        assert_eq!(h.request("GET","/tasks",json!({}),Some(KEY),origin).0,if cors{200}else{403});
        let mut stream=TcpStream::connect(h.addr).unwrap();
        write!(stream,"OPTIONS /tasks HTTP/1.1\r\nHost: {}\r\nOrigin: https://any-site.example\r\nAccess-Control-Request-Method: GET\r\n\r\n",h.addr).unwrap();
        let mut raw=String::new();stream.read_to_string(&mut raw).unwrap();
        assert_eq!(raw.contains("Access-Control-Allow-Origin: *"),cors);
        assert!(!raw.contains("Access-Control-Allow-Credentials"));
    }
}

#[test]
fn qt_task_and_tool_fields_remain_available() {
    let source = include_str!("../../include/Types.h");
    let task = source
        .split("struct TaskInstance {")
        .nth(1)
        .unwrap()
        .split("struct ApiResponse")
        .next()
        .unwrap();
    let mut expected = BTreeSet::new();
    for line in task.lines() {
        if let Some((_, rest)) = line.split_once("j[\"") {
            expected.insert(rest.split('"').next().unwrap().to_owned());
        }
    }
    let actual = macrohelp_wayland::task_registry::task(
        &json!({"number":1,"task":"fixture","state":"scheduled","due_ms":9999999999999u64,"created_ms":1,"error":"example","description":"fixture","loop_times":1,"delay_seconds":1}),
        true,
    );
    for key in expected {
        assert!(actual.get(&key).is_some(), "Missing Qt task field: {key}");
    }
    let h = Harness::new(false);
    let root = h.ok("GET", "/", json!({}));
    assert!(root["tasks_directory"].is_string());
    for tool in root["tools"].as_array().unwrap() {
        for key in ["name", "description", "endpoint", "method", "example"] {
            assert!(tool[key].is_string(), "Missing tool field: {key}");
        }
    }
}

#[test]
fn bounded_http_framing_and_deadline() {
    let h = Harness::new(false);
    for (extra, expected) in [
        ("Transfer-Encoding: chunked\r\n", 400),
        ("Content-Length: 12\r\n", 400),
        ("Expect: 100-continue\r\n", 417),
    ] {
        assert_eq!(
            h.request("POST", "/run", json!({}), Some(KEY), extra).0,
            expected
        );
    }
    let mut stream = TcpStream::connect(h.addr).unwrap();
    stream
        .set_read_timeout(Some(Duration::from_secs(5)))
        .unwrap();
    write!(
        stream,
        "POST /run HTTP/1.1\r\nHost: {}\r\nX-Tasket-Key: {KEY}\r\nContent-Length: 99\r\n\r\n{{",
        h.addr
    )
    .unwrap();
    let before = Instant::now();
    let mut response = String::new();
    stream.read_to_string(&mut response).unwrap();
    assert!(response.starts_with("HTTP/1.1 408"), "{response}");
    assert!(before.elapsed() < Duration::from_secs(5));
    // A stalled upload does not poison the shared queue or other workers.
    h.ok("GET", "/health", json!({}));
}
#[test]
fn strict_query_parsing() {
    assert_eq!(
        split_target("/run?task=hello+world&delay=0").unwrap().1["task"],
        "hello world"
    );
    assert!(split_target("/run?task=%GG").is_err());
    assert!(split_target("/run?delay=1&delay=2").is_err());
}
#[test]
fn http_auth_and_validation() {
    let h = Harness::new(false);
    assert_eq!(h.request("GET", "/health", json!({}), None, "").0, 401);
    assert_eq!(
        h.request("GET", "/health", json!({}), Some("bad"), "").0,
        403
    );
    assert_eq!(
        h.request(
            "POST",
            "/stop",
            json!({}),
            Some(KEY),
            "Origin: https://example.invalid\r\n"
        )
        .0,
        403
    );
    for route in ["/check?id=no", "/check", "/run?task=../bad", "/stop?id=0"] {
        assert_eq!(
            h.req(
                if route.starts_with("/stop") {
                    "POST"
                } else {
                    "GET"
                },
                route,
                json!({})
            )
            .0,
            400
        );
    }
    assert_eq!(h.req("GET", "/not-a-route", json!({})).0, 404);
    let denied = json!({"name":"denied","task":{"docType":"ScheduleTask File","actions":[{"type":"systemcommand","sysCommandType":"shutdown"}]}});
    assert_eq!(h.req("POST", "/temp-task", denied).0, 403);
    assert_eq!(h.req("POST","/temp-task",json!({"name":"bad","run":false,"cleanup":true,"task":{"actions":[{"type":"wait","duration":-1}]}})).0,400);
}
#[test]
fn all_http_routes_preserve_contract_and_native_schedules() {
    let h = Harness::new(false);
    assert_eq!(h.ok("GET", "/health", json!({}))["shared_queue"], true);
    assert_eq!(
        h.ok("GET", "/", json!({}))["tools"]
            .as_array()
            .unwrap()
            .len(),
        ROUTES.len()
    );
    h.ok("POST","/temp-task",json!({"name":"saved","run":false,"task":{"docType":"ScheduleTask File","description":"native fixture","actions":[{"type":"wait","duration":0}]}}));
    assert!(h.dir.join("schedules/saved.omscht").is_file());
    assert_eq!(h.ok("GET", "/tasks", json!({}))["count"], 1);
    let one = h.ok("GET", "/run?task=SAVED.SCHT&delay=60&loop=3", json!({}));
    assert_eq!(one["loop_times"], 3);
    assert!(one["created_at"].as_str().unwrap().contains('T'));
    let two = h.ok(
        "POST",
        "/run",
        json!({"task":"saved","delay":60,"loop":"infinite"}),
    );
    assert_eq!(two["loop_times"], -1);
    let id = one["task_number"].as_u64().unwrap();
    let check = h.ok("GET", &format!("/check?id={id}"), json!({}));
    assert_eq!(check["state"], "scheduled");
    assert!(check.get("remaining_seconds").is_some());
    assert!(check.get("started_at").is_some());
    assert_eq!(
        h.ok("GET", &format!("/task?id={id}"), json!({}))["task_number"],
        id
    );
    h.ok("POST", &format!("/stop?id={id}"), json!({}));
    assert_eq!(
        h.ok("GET", &format!("/check?id={id}"), json!({}))["state"],
        "stopped"
    );
    h.ok(
        "POST",
        "/entrypoint",
        json!({"id":"ep1","value":"hello","type":"string"}),
    );
    assert_eq!(
        h.ok("GET", "/entrypoints", json!({}))["entrypoints"]["ep1"],
        "hello"
    );
    h.ok("POST", "/grid", json!({"id":"grid-0","value":"42"}));
    assert_eq!(h.ok("GET", "/grid", json!({}))["cells"]["grid-0"], "42");
    assert_eq!(h.ok("GET", "/status", json!({}))["scheduled_count"], 1);
    h.ok("POST", "/stop", json!({}));
    assert_eq!(h.ok("GET", "/status", json!({}))["scheduled_count"], 0);
    assert_eq!(
        h.req(
            "POST",
            "/temp-task",
            json!({"name":"saved","task":{"actions":[]}})
        )
        .0,
        409
    );
}
#[test]
fn all_native_action_types_and_memory_temp_cleanup() {
    let h = Harness::new(true);
    h.ok(
        "POST",
        "/temp-task",
        json!({"name":"child","run":false,"task":{"actions":[{"type":"wait","duration":0}]}}),
    );
    let out=h.ok("POST","/temp-task",json!({"name":"temporary","cleanup":true,"delay":0,"loop":2,"task":{"docType":"ScheduleTask File","actions":[{"type":"paste","content":"hello"},{"type":"wait","duration":0.001},{"type":"keyssequence","keysmap":{"0":[1,["ctrl","c"]]}},{"type":"cursormovements","cursormovsmap":[[0,1,10,10]]},{"type":"systemcommand","sysCommandType":"openurl","sysCommandParam1":"https://example.invalid"},{"type":"runningothertask","otherTaskName":"child.omscht","otherTaskLoops":1}]}}));
    assert_eq!(out["storage"], "memory");
    assert!(!h.dir.join("schedules/temporary.omscht").exists());
    let id = out["task"]["task_number"].as_u64().unwrap();
    let deadline = Instant::now() + Duration::from_secs(3);
    loop {
        let job = h.ok("GET", &format!("/check?id={id}"), json!({}));
        if job["state"] == "finished" {
            assert_eq!(job["completed_loops"], 2);
            break;
        }
        assert!(Instant::now() < deadline, "{job}");
        std::thread::sleep(Duration::from_millis(10));
    }
}
