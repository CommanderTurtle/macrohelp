//! Private overlay state, authenticated loopback client and Hyprland read-only IPC.
use crate::client::{ApiError, Config, Result, string};
use crate::heatmap;
use serde_json::{Value, json};
use std::{
    fs,
    io::{BufRead, Read, Write},
    net::TcpStream,
    path::Path,
    process::{Command, Stdio},
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    time::{Duration, Instant},
};
const LIMIT: u64 = 16 * 1024 * 1024;
static STOP: AtomicBool = AtomicBool::new(false);
#[derive(Default)]
struct DialogKeys {
    held: std::collections::BTreeSet<u64>,
}
impl DialogKeys {
    fn accept(&mut self, sample: &Value) -> Vec<u8> {
        if sample["locked"] == true || sample["available"] != true {
            self.held.clear();
            return vec![];
        }
        let mut digits = vec![];
        if let Some(events) = sample["events"].as_array() {
            for event in events {
                let Some(code) = event[0].as_u64() else {
                    continue;
                };
                if event[1] == 1 {
                    let fresh = self.held.insert(code);
                    // Evdev 2..10 = 1..9, 11 = 0; right Alt is 100, not Down (108).
                    if fresh
                        && (2..=11).contains(&code)
                        && (self.held.contains(&56) || self.held.contains(&100))
                    {
                        digits.push(if code == 11 { 0 } else { code as u8 - 1 });
                    }
                } else {
                    self.held.remove(&code);
                }
            }
        }
        // Reconcile a bounded-buffer overrun without replaying held shortcuts.
        self.held = sample["held"]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(Value::as_u64)
            .collect();
        digits
    }
}
#[cfg(unix)]
extern "C" fn interrupted(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}
pub fn http(config: &Config, method: &str, path: &str, body: &Value) -> Result<Value> {
    if config.key.is_empty() {
        return Err(ApiError::new(503, "Controller not configured"));
    }
    if path.contains(['\r', '\n', ' ']) || !path.starts_with('/') {
        return Err(ApiError::new(400, "Invalid local HTTP path"));
    }
    let mut stream = TcpStream::connect_timeout(&config.address, Duration::from_secs(2))
        .map_err(|e| ApiError::new(503, format!("Start macrohelp-httpd first: {e}")))?;
    stream
        .set_read_timeout(Some(Duration::from_secs(5)))
        .map_err(io_error)?;
    stream
        .set_write_timeout(Some(Duration::from_secs(5)))
        .map_err(io_error)?;
    let text = body.to_string();
    write!(stream,"{method} {path} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\nX-Tasket-Key: {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\r\n{text}",config.address,config.key,text.len()).map_err(io_error)?;
    let mut bytes = Vec::new();
    stream
        .take(LIMIT + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| ApiError::new(503, "Controller unavailable"))?;
    if bytes.len() as u64 > LIMIT {
        return Err(ApiError::new(502, "HTTP response too large"));
    }
    let text = std::str::from_utf8(&bytes).map_err(|e| ApiError::new(502, e.to_string()))?;
    let (headers, body) = text
        .split_once("\r\n\r\n")
        .ok_or_else(|| ApiError::new(502, "Incomplete HTTP response"))?;
    let status = headers
        .split_whitespace()
        .nth(1)
        .and_then(|s| s.parse::<u16>().ok())
        .ok_or_else(|| ApiError::new(502, "Invalid HTTP status"))?;
    let value: Value = serde_json::from_str(body).map_err(|e| ApiError::new(502, e.to_string()))?;
    if status >= 400 || value["success"] != true {
        return Err(ApiError::new(
            status,
            value["message"].as_str().unwrap_or("Controller failed"),
        ));
    }
    Ok(value["data"].clone())
}
fn io_error(e: std::io::Error) -> ApiError {
    ApiError::new(500, e.to_string())
}
fn input_sample(display: bool, heatmap: bool) -> Result<Value> {
    let unlocked = (!display && !heatmap) || hypr("locked")?["locked"] == false;
    let code = format!(
        "repl return _G.__macrohelp_input and _G.__macrohelp_input.sample({},{}) or '{{\"initialize\":true}}'",
        display && unlocked,
        heatmap && unlocked
    );
    let mut value = hypr(&code)?;
    if value["initialize"] == true {
        let boot = hypr(&format!("repl {}", include_str!("../input.lua")))?;
        if boot["ready"] != true {
            return Err(ApiError::new(503, "Native key events unavailable"));
        }
        value = hypr(&code)?;
    }
    value["locked"] = json!(!unlocked);
    Ok(value)
}
pub fn hypr(command: &str) -> Result<Value> {
    #[cfg(unix)]
    {
        use std::os::unix::net::UnixStream;
        let runtime = std::env::var("XDG_RUNTIME_DIR")
            .map_err(|_| ApiError::new(503, "XDG_RUNTIME_DIR is not set"))?;
        let signature = std::env::var("HYPRLAND_INSTANCE_SIGNATURE")
            .map_err(|_| ApiError::new(503, "Hyprland is not running"))?;
        if signature.contains('/') || signature.contains("..") {
            return Err(ApiError::new(400, "Invalid Hyprland instance signature"));
        }
        let mut socket = UnixStream::connect(format!("{runtime}/hypr/{signature}/.socket.sock"))
            .map_err(io_error)?;
        socket
            .set_read_timeout(Some(Duration::from_millis(400)))
            .map_err(io_error)?;
        socket
            .set_write_timeout(Some(Duration::from_millis(400)))
            .map_err(io_error)?;
        socket
            .write_all(format!("j/{command}").as_bytes())
            .map_err(io_error)?;
        let mut data = String::new();
        socket
            .take(1024 * 1024)
            .read_to_string(&mut data)
            .map_err(io_error)?;
        serde_json::from_str(&data).map_err(|e| ApiError::new(502, e.to_string()))
    }
    #[cfg(not(unix))]
    {
        let _ = command;
        Err(ApiError::new(
            503,
            "Cursor queries require Hyprland on Unix",
        ))
    }
}
pub fn default_state() -> Value {
    json!({"version":1,"script":"","registry":"","paths":{"import":"","export":""},"shells":{"X":"bash","C":"sh","G":"uv run python","P":"pwsh","T":""},"vars":{"A":"","S":"","D":"","F":""},"buffers":{"Z":"","X":"","C":"","V":""},"zones":{"Z":null,"X":null,"C":null,"V":null},"points":vec![Value::Null;16],"view":{"crosshair":true,"big_crosshair":false,"grid":false,"coordinates":true,"panel":true,"keys":true,"coordinates_only":false},"last_task":null})
}
pub fn validate_state(value: &Value) -> Result<()> {
    if value["version"] != 1
        || !value["script"].is_string()
        || !value["registry"].is_string()
        || !value["zones"].is_object()
        || value["points"].as_array().is_none_or(|p| p.len() != 16)
    {
        return Err(ApiError::new(400, "Invalid Macrohelp environment"));
    }
    for key in [
        "crosshair",
        "big_crosshair",
        "grid",
        "coordinates",
        "keys",
        "coordinates_only",
    ] {
        if !value["view"][key].is_boolean() {
            return Err(ApiError::new(400, "Invalid view preference"));
        }
    }
    for (key, names) in [
        ("buffers", &["Z", "X", "C", "V"][..]),
        ("vars", &["A", "S", "D", "F"][..]),
        ("shells", &["X", "C", "G"][..]),
    ] {
        for name in names {
            if !value[key][name].is_string() {
                return Err(ApiError::new(400, "Missing text buffer/variable/alias"));
            }
        }
    }
    // Optional additions preserve existing v1 environments and preset exports.
    for key in ["P", "T"] {
        if value["shells"].get(key).is_some_and(|v| !v.is_string()) {
            return Err(ApiError::new(
                400,
                "Invalid shell command/terminal shortcut",
            ));
        }
    }
    if value["view"].get("panel").is_some_and(|v| !v.is_boolean()) {
        return Err(ApiError::new(400, "Invalid coordinate panel preference"));
    }
    if let Some(paths) = value.get("paths") {
        for key in ["import", "export"] {
            if !paths[key].is_string() {
                return Err(ApiError::new(400, "Invalid transfer path"));
            }
        }
    }
    for point in value["points"].as_array().unwrap() {
        if !point.is_null() {
            coordinates(point, 2)?;
        }
    }
    for key in ["Z", "X", "C", "V"] {
        if value["zones"].get(key).is_none() {
            return Err(ApiError::new(400, "Missing zone slot"));
        }
        if !value["zones"][key].is_null() {
            coordinates(&value["zones"][key], 4)?;
        }
    }
    if value.to_string().len() > 1024 * 1024 {
        return Err(ApiError::new(400, "Environment exceeds 1 MiB"));
    }
    Ok(())
}
fn coordinates(value: &Value, count: usize) -> Result<Vec<i64>> {
    let a = value
        .as_array()
        .filter(|a| a.len() == count)
        .ok_or_else(|| ApiError::new(400, "Invalid coordinate array"))?;
    a.iter()
        .map(|v| {
            v.as_i64()
                .filter(|n| (-100000..=100000).contains(n))
                .ok_or_else(|| ApiError::new(400, "Coordinate must be an integer within ±100000"))
        })
        .collect()
}
fn write_state(path: &Path, value: &Value, replace: bool) -> Result<()> {
    validate_state(value)?;
    let parent = path
        .parent()
        .ok_or_else(|| ApiError::new(400, "Missing parent directory"))?;
    fs::create_dir_all(parent).map_err(io_error)?;
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        use std::os::unix::fs::PermissionsExt;
        if fs::symlink_metadata(parent)
            .map_err(io_error)?
            .file_type()
            .is_symlink()
        {
            return Err(ApiError::new(400, "State parent may not be a symlink"));
        }
        options.mode(0o600);
        if replace {
            fs::set_permissions(parent, fs::Permissions::from_mode(0o700)).map_err(io_error)?;
        }
    }
    let temp = if replace {
        path.with_extension(format!("{}.tmp", std::process::id()))
    } else {
        path.to_path_buf()
    };
    let mut created = false;
    let result = (|| {
        let mut file = options.open(&temp).map_err(io_error)?;
        created = true;
        file.write_all(serde_json::to_string_pretty(value).unwrap().as_bytes())
            .map_err(io_error)?;
        file.sync_all().map_err(io_error)?;
        if replace {
            fs::rename(&temp, path).map_err(io_error)?;
        }
        Ok(())
    })();
    if result.is_err() && replace && created {
        let _ = fs::remove_file(temp);
    }
    result
}
fn read_state(path: &Path) -> Result<Value> {
    let m = fs::symlink_metadata(path).map_err(io_error)?;
    if !m.is_file() || m.len() > 1024 * 1024 {
        return Err(ApiError::new(
            400,
            "Environment must be a regular file under 1 MiB",
        ));
    }
    let text = fs::read_to_string(path).map_err(io_error)?;
    let mut state: Value = if text.trim_start().starts_with('{') {
        serde_json::from_str(&text).map_err(|e| ApiError::new(400, e.to_string()))?
    } else {
        import_legacy(&text)?
    };
    let missing: Vec<(String, Value)> = default_state()["view"]
        .as_object()
        .unwrap()
        .iter()
        .filter(|(k, _)| state["view"].get(k).is_none())
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();
    for (key, value) in missing {
        state["view"][key] = value;
    }
    validate_state(&state)?;
    for key in ["P", "T"] {
        if state["shells"].get(key).is_none() {
            state["shells"][key] = default_state()["shells"][key].clone();
        }
    }
    if state.get("paths").is_none() {
        state["paths"] = default_state()["paths"].clone();
    }
    if state["view"].get("panel").is_none() {
        state["view"]["panel"] = json!(true);
    }
    Ok(state)
}
pub fn import_legacy(text: &str) -> Result<Value> {
    let mut v = default_state();
    let mut found = false;
    for line in text.lines() {
        let Some((key, raw)) = line.split_once('=') else {
            continue;
        };
        let value = unescape(raw);
        let key = key.trim();
        if !key.is_ascii() {
            continue;
        }
        let lower = key.to_ascii_lowercase();
        if lower == "macrohelpregistryhubexportversion" {
            found = value == "1";
        }
        match lower.as_str() {
            "script" => v["script"] = json!(value),
            "registryfilepath" => v["registry"] = json!(value),
            "shellaliasx" => v["shells"]["X"] = json!(value),
            "shellaliasc" => v["shells"]["C"] = json!(value),
            "shellaliasg" => v["shells"]["G"] = json!(value),
            "shellaliasp" => v["shells"]["P"] = json!(value),
            "shellaliast" => v["shells"]["T"] = json!(value),
            "importpath" => v["paths"]["import"] = json!(value),
            "exportpath" => v["paths"]["export"] = json!(value),
            "lasttaskettaskstate" => v["last_task"]["state"] = json!(value),
            "lasttaskettaskmessage" => v["last_task"]["message"] = json!(value),
            "lasttaskettasknumber" => {
                if let Ok(n) = value.parse::<u64>() {
                    v["last_task"]["task_number"] = json!(n);
                }
            }
            "crosshairvisible" => v["view"]["crosshair"] = json!(value == "1" || value == "true"),
            "gridvisible" => v["view"]["grid"] = json!(value == "1" || value == "true"),
            "keyhudvisible" => v["view"]["keys"] = json!(value == "1" || value == "true"),
            "coordinatepanelvisible" => v["view"]["panel"] = json!(value == "1" || value == "true"),
            "cursorcoordinatesvisible" => {
                v["view"]["coordinates"] = json!(value == "1" || value == "true")
            }
            "coordinatesonlymode" => {
                v["view"]["coordinates_only"] = json!(value == "1" || value == "true")
            }
            _ => {
                if lower.starts_with("var") && lower.len() == 4 {
                    v["vars"][key[3..].to_ascii_uppercase()] = json!(value);
                } else if lower.starts_with("paste") && lower.len() == 6 {
                    v["buffers"][key[5..].to_ascii_uppercase()] = json!(value);
                } else if lower.starts_with("zone") && lower.len() == 5 {
                    let a: Vec<_> = value
                        .split(',')
                        .filter_map(|n| n.trim().parse::<i64>().ok())
                        .collect();
                    if a.len() == 4 {
                        v["zones"][key[4..].to_ascii_uppercase()] = json!(a);
                    }
                } else if lower.starts_with("point")
                    && !lower.ends_with("set")
                    && let Ok(n) = lower[5..].parse::<usize>()
                {
                    let a: Vec<_> = value
                        .split(',')
                        .filter_map(|n| n.trim().parse::<i64>().ok())
                        .collect();
                    if (1..=16).contains(&n) && a.len() == 2 {
                        v["points"][n - 1] = json!(a);
                    }
                }
            }
        }
    }
    if !found {
        return Err(ApiError::new(400, "Not a Macrohelp Registry Hub export"));
    }
    Ok(v)
}
fn unescape(s: &str) -> String {
    let mut out = String::new();
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('n') => out.push('\n'),
                Some('r') => out.push('\r'),
                Some('t') => out.push('\t'),
                Some('\\') => out.push('\\'),
                Some(c) => {
                    out.push('\\');
                    out.push(c)
                }
                None => out.push('\\'),
            }
        } else {
            out.push(c)
        }
    }
    out
}
fn command(program: &str, args: &[&str], input: Option<Vec<u8>>) -> Result<Vec<u8>> {
    let mut command = Command::new(program);
    command
        .args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null());
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        command.process_group(0);
    }
    let mut child = command
        .spawn()
        .map_err(|e| ApiError::new(503, format!("Cannot start {program}: {e}")))?;
    let stdin = child.stdin.take();
    let writer = std::thread::spawn(move || {
        if let (Some(mut stdin), Some(input)) = (stdin, input) {
            let _ = stdin.write_all(&input);
        }
    });
    let output = child.stdout.take().unwrap();
    let reader = std::thread::spawn(move || {
        let mut out = Vec::new();
        output.take(LIMIT + 1).read_to_end(&mut out).map(|_| out)
    });
    let until = Instant::now() + Duration::from_secs(15);
    let success = loop {
        if let Some(status) = child.try_wait().map_err(io_error)? {
            break status.success();
        }
        if Instant::now() > until || STOP.load(Ordering::Relaxed) {
            #[cfg(unix)]
            unsafe {
                libc::kill(-(child.id() as i32), libc::SIGKILL);
            }
            let _ = child.kill();
            let _ = child.wait();
            break false;
        }
        std::thread::sleep(Duration::from_millis(10));
    };
    let _ = writer.join();
    let data = reader
        .join()
        .map_err(|_| ApiError::new(500, "Reader stopped"))?
        .map_err(io_error)?;
    if data.len() as u64 > LIMIT {
        return Err(ApiError::new(413, "Tool output exceeds 16 MiB"));
    }
    if !success {
        return Err(ApiError::new(503, format!("{program} failed or timed out")));
    }
    Ok(data)
}
fn handle(config: &Config, r: &Value) -> Result<Value> {
    match string(r, "method")? {
        "hello" => Ok(
            json!({"state":if config.state_dir.join("overlay.json").exists(){read_state(&config.state_dir.join("overlay.json"))?}else{default_state()},"monitors":hypr("monitors").unwrap_or(json!([])),"endpoint":format!("http://{}",config.address)}),
        ),
        "http" => {
            let verb = string(r, "verb")?;
            if !["GET", "POST"].contains(&verb) {
                return Err(ApiError::new(400, "Only GET/POST supported"));
            }
            http(config, verb, string(r, "path")?, &r["body"])
        }
        "stop" => http(config, "POST", "/stop", &json!({})),
        "save-state" => {
            write_state(&config.state_dir.join("overlay.json"), &r["state"], true)?;
            Ok(json!(true))
        }
        "export" => {
            validate_state(&r["state"])?;
            if r["format"] == "windows" {
                crate::artifacts::write(
                    Path::new(string(r, "path")?),
                    &crate::artifacts::legacy(&r["state"]),
                    None,
                )?;
            } else {
                write_state(Path::new(string(r, "path")?), &r["state"], false)?;
            }
            Ok(json!(true))
        }
        "export-actions" => crate::artifacts::actions(r),
        "append-actions" => crate::artifacts::append_session(r),
        "import" => read_state(Path::new(string(r, "path")?)),
        "bindings" => Ok(hypr("binds").unwrap_or(json!([]))),
        "profile-list" => crate::profiles::list(&config.state_dir.join("profiles")),
        "profile-save" => crate::profiles::save(&config.state_dir.join("profiles"), r),
        "profile-import" => {
            crate::profiles::import(&config.state_dir.join("profiles"), string(r, "name")?)
        }
        "regedited" => {
            let action = string(r, "action")?;
            let file = string(r, "file")?;
            let reference = string(r, "reference")?;
            if reference.starts_with('-') || file.starts_with('-') {
                return Err(ApiError::new(
                    400,
                    "Regedited file/reference may not be flags",
                ));
            }
            let args = if action == "append" {
                vec!["rs", file, reference, "--append"]
            } else if action == "clip" {
                vec!["rg", file, reference, "--clip"]
            } else {
                return Err(ApiError::new(400, "Unknown regedited action"));
            };
            let input = if action == "append" {
                Some(command("wl-paste", &["--no-newline"], None)?)
            } else {
                None
            };
            command("rgd", &args, input)?;
            Ok(json!(true))
        }
        _ => Err(ApiError::new(400, "Unknown overlay request")),
    }
}
fn reply(request: &Value, result: Result<Value>) -> Value {
    match result {
        Ok(value) => json!({"id":request["id"],"ok":true,"result":value}),
        Err(e)
            if e.status >= 500 && matches!(request["method"].as_str(), Some("http" | "stop")) =>
        {
            json!({"id":request["id"],"ok":false,"offline":true})
        }
        Err(e) => json!({"id":request["id"],"ok":false,"error":e.message,"code":e.status}),
    }
}
pub fn run() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help") {
        println!(
            "macrohelpctl [session|stop|health|mcp] [--env PATH]\nOverlay client and MCP adapter; execution still requires the separate HTTP daemon."
        );
        return Ok(());
    }
    let mode = args.first().map(String::as_str).unwrap_or("session");
    let default_path = crate::client::default_env();
    let path = args
        .iter()
        .position(|a| a == "--env")
        .and_then(|i| args.get(i + 1))
        .map(String::as_str)
        .unwrap_or_else(|| default_path.to_str().unwrap_or(".env"));
    let config = Config::load(Path::new(path))?;
    if mode == "mcp" {
        let settings =
            crate::control_http::Settings::load(&config)?.ok_or("Overlay control is disabled")?;
        return crate::control_mcp::run(&settings).map_err(Into::into);
    }
    if mode == "stop" {
        let result = http(&config, "POST", "/stop", &json!({}));
        if result.as_ref().is_err_and(|e| e.status >= 500) {
            return Ok(());
        }
        result?;
        println!("Stopped");
        return Ok(());
    }
    if mode == "health" {
        match http(&config, "GET", "/health", &json!({})) {
            Ok(value) => println!("{value}"),
            Err(e) if e.status >= 500 => (),
            Err(e) => return Err(e.into()),
        };
        return Ok(());
    }
    if mode != "session" {
        return Err("Expected session, stop or health".into());
    }
    #[cfg(unix)]
    unsafe {
        libc::signal(libc::SIGINT, interrupted as *const () as usize);
        libc::signal(libc::SIGTERM, interrupted as *const () as usize);
    }
    let (sender, receiver) = mpsc::channel();
    std::thread::spawn(move || {
        let mut reader = std::io::stdin().lock();
        loop {
            let mut line = String::new();
            match reader
                .by_ref()
                .take(2 * 1024 * 1024 + 1)
                .read_line(&mut line)
            {
                Ok(0) | Err(_) => break,
                Ok(_) => {
                    if line.len() > 2 * 1024 * 1024 {
                        break;
                    }
                    if sender.send(line).is_err() {
                        break;
                    }
                }
            }
        }
    });
    let mut tracking = true;
    let control_bridge = Arc::new(crate::control_http::Bridge::default());
    let mut control_server: Option<crate::control_http::Server> = None;
    let mut keys = false;
    let mut dialog_keys = DialogKeys::default();
    let heatmap_path = config.state_dir.join("heatmap.json");
    let (mut book, heatmap_load_error) = match heatmap::load(&heatmap_path) {
        Ok(value) => (value, None),
        Err(error) => (heatmap::empty(), Some(error.message)),
    };
    let mut requests: Vec<std::thread::JoinHandle<()>> = Vec::new();
    let mut last_tick = Instant::now();
    let mut last_save = Instant::now();
    let mut heatmap_dirty = false;
    let mut input_available = None;
    let mut last_input = Instant::now() - Duration::from_secs(1);
    let mut last = json!(null);
    let mut last_poll = Instant::now() - Duration::from_secs(1);
    let mut last_monitors = Instant::now() - Duration::from_secs(6);
    let mut scale: f64 = 1.0;
    while !STOP.load(Ordering::Relaxed) {
        match receiver.recv_timeout(Duration::from_millis(if tracking { 16 } else { 100 })) {
            Ok(line) => {
                let response = match serde_json::from_str::<Value>(&line) {
                    Ok(r)
                        if matches!(r["method"].as_str(), Some("http" | "stop" | "regedited")) =>
                    {
                        // Slow controller/tools I/O must not stall the pointer/key HUD.
                        requests.retain(|worker| !worker.is_finished());
                        if requests.len() >= 8 {
                            Some(reply(&r, Err(ApiError::new(429, "Local client is busy"))))
                        } else {
                            let config = config.clone();
                            requests.push(std::thread::spawn(move || {
                                println!("{}", reply(&r, handle(&config, &r)));
                                let _ = std::io::stdout().flush();
                            }));
                            None
                        }
                    }
                    Ok(r) => {
                        let result = if r["method"] == "control-start" {
                            if let Some(server) = &control_server {
                                Ok(json!({"enabled":true,"address":server.address.to_string()}))
                            } else {
                                crate::control_http::Settings::load(&config).and_then(|settings| {
                                    if let Some(settings) = settings {
                                        let address = settings.address.to_string();
                                        control_server = Some(crate::control_http::Server::bind(
                                            settings,
                                            control_bridge.dispatch(),
                                        )?);
                                        Ok(json!({"enabled":true,"address":address}))
                                    } else {
                                        Ok(json!({"enabled":false}))
                                    }
                                })
                            }
                        } else if r["method"] == "control-live" {
                            Ok(json!(
                                control_bridge.live(r["ticket"].as_u64().unwrap_or(0))
                            ))
                        } else if r["method"] == "control-reply" {
                            control_bridge.reply(&r)
                        } else if r["method"] == "tracking" {
                            tracking = r["enabled"].as_bool().unwrap_or(false);
                            Ok(json!(true))
                        } else if r["method"] == "keys" {
                            keys = r["enabled"].as_bool().unwrap_or(false);
                            if !keys && !tracking && !heatmap::active(&book) {
                                let _ = input_sample(false, false);
                            }
                            Ok(json!(true))
                        } else if r["method"] == "heatmap" && heatmap_load_error.is_some() {
                            Err(ApiError::new(400, heatmap_load_error.as_deref().unwrap()))
                        } else if r["method"] == "heatmap"
                            && r["action"].as_str().unwrap_or("snapshot") == "snapshot"
                        {
                            Ok(book.clone())
                        } else if r["method"] == "heatmap" {
                            // Flush old-session aggregates before switching the active session.
                            if let Ok(sample) =
                                input_sample(keys || tracking, heatmap::collecting(&book))
                            {
                                heatmap::add(&mut book, &sample["counts"]);
                            }
                            let mut next = book.clone();
                            let now = std::time::SystemTime::now()
                                .duration_since(std::time::UNIX_EPOCH)
                                .unwrap_or_default()
                                .as_secs();
                            heatmap::command(&mut next, &r, now).and_then(|()| {
                                heatmap::save(&heatmap_path, &next)?;
                                book = next;
                                heatmap_dirty = false;
                                last_tick = Instant::now();
                                let _ = input_sample(keys || tracking, heatmap::collecting(&book));
                                Ok(book.clone())
                            })
                        } else {
                            handle(&config, &r)
                        };
                        Some(reply(&r, result))
                    }
                    Err(e) => Some(json!({"ok":false,"error":e.to_string()})),
                };
                if let Some(response) = response {
                    println!("{response}");
                }
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
            Err(mpsc::RecvTimeoutError::Timeout) => (),
        }
        if tracking && last_poll.elapsed() >= Duration::from_millis(8) {
            last_poll = Instant::now();
            if let Ok(cursor) = hypr("cursorpos")
                && cursor != last
            {
                let physical = json!({
                    "x": cursor["x"].as_f64().unwrap_or(0.0) * scale,
                    "y": cursor["y"].as_f64().unwrap_or(0.0) * scale,
                });
                println!(
                    "{}",
                    json!({"event":"cursor","cursor":cursor,"physical":physical})
                );
                last = cursor;
            }
        }
        let tick_due = last_tick.elapsed() >= Duration::from_secs(1);
        let input_interval = Duration::from_millis(if keys || tracking { 33 } else { 500 });
        if (keys || tracking || heatmap::active(&book))
            && (last_input.elapsed() >= input_interval || tick_due)
        {
            last_input = Instant::now();
            match input_sample(keys || tracking, heatmap::collecting(&book)) {
                Ok(sample) => {
                    if sample["counts"].as_object().is_some_and(|m| !m.is_empty()) {
                        heatmap::add(&mut book, &sample["counts"]);
                        heatmap_dirty = true;
                    }
                    let available = sample["available"] == true;
                    if input_available != Some(available)
                        || sample["locked"] == true
                        || sample["events"].as_array().is_some_and(|a| !a.is_empty())
                    {
                        println!("{}", json!({"event":"keys","sample":sample}));
                    }
                    input_available = Some(available);
                    for digit in dialog_keys.accept(&sample) {
                        println!("{}", json!({"event":"dialog","key":digit.to_string()}));
                    }
                }
                Err(_) => {
                    if input_available != Some(false) {
                        println!(
                            "{}",
                            json!({"event":"keys","sample":{"available":false,"events":[],"held":[]}})
                        );
                    }
                    input_available = Some(false);
                    // Capability retry, not package-version branching.
                    last_input = Instant::now() + Duration::from_millis(1900);
                }
            }
        }
        if tick_due {
            let seconds = last_tick.elapsed().as_secs();
            last_tick += Duration::from_secs(seconds);
            if heatmap::active(&book) {
                let collecting = heatmap::collecting(&book);
                heatmap::tick(&mut book, seconds);
                if collecting != heatmap::collecting(&book) {
                    let _ = input_sample(keys || tracking, heatmap::collecting(&book));
                }
                heatmap_dirty = true;
                println!(
                    "{}",
                    json!({"event":"heatmap-session","active":book["active"],"session":heatmap::current(&book)})
                );
            }
        }
        if heatmap_dirty && last_save.elapsed() >= Duration::from_secs(5) {
            match heatmap::save(&heatmap_path, &book) {
                Ok(()) => heatmap_dirty = false,
                Err(e) => println!("{}", json!({"event":"heatmap-error","error":e.message})),
            }
            last_save = Instant::now();
        }
        if tracking && last_monitors.elapsed() >= Duration::from_secs(5) {
            last_monitors = Instant::now();
            if let Ok(monitors) = hypr("monitors") {
                if let Some(first) = monitors.as_array().and_then(|a| a.first()) {
                    if let Some(s) = first["scale"].as_f64() {
                        scale = s;
                    }
                }
                println!("{}", json!({"event":"monitors","monitors":monitors}));
            }
        }
        std::io::stdout().flush()?;
    }
    if keys || tracking || heatmap::active(&book) {
        if let Ok(sample) = input_sample(keys || tracking, heatmap::collecting(&book)) {
            heatmap::add(&mut book, &sample["counts"]);
        }
        let _ = input_sample(false, false);
    }
    if heatmap_dirty || heatmap::active(&book) {
        heatmap::save(&heatmap_path, &book)?;
    }
    STOP.store(true, Ordering::Relaxed);
    control_bridge.close();
    drop(control_server);
    for worker in requests {
        let _ = worker.join();
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn dialog_shortcuts_use_physical_digits_and_accept_fast_taps() {
        let mut keys = DialogKeys::default();
        for digit in 0..10 {
            let code = if digit == 0 { 11 } else { digit + 1 };
            assert_eq!(
                keys.accept(
                    &json!({"available":true,"events":[[56,1],[code,1],[code,0],[56,0]],"held":[]})
                ),
                vec![digit as u8]
            );
        }
        assert!(
            keys.accept(&json!({"available":true,"events":[[108,1],[2,1]],"held":[108,2]}))
                .is_empty()
        );
        keys.accept(&json!({"available":true,"events":[],"held":[]}));
        assert_eq!(
            keys.accept(&json!({"available":true,"events":[[100,1],[2,1]],"held":[100,2]})),
            vec![1]
        );
        assert!(
            keys.accept(&json!({"available":true,"events":[[2,1]],"held":[100,2]}))
                .is_empty()
        );
        assert_eq!(
            keys.accept(&json!({"available":true,"events":[[2,0],[2,1]],"held":[100,2]})),
            vec![1]
        );
        assert!(
            keys.accept(
                &json!({"available":true,"locked":true,"events":[[56,1],[2,1]],"held":[56,2]})
            )
            .is_empty()
        );
    }
    #[test]
    fn state_and_legacy_transfer() {
        let v=import_legacy("MacrohelpRegistryHubExportVersion=1\nScript={paste A}\\n{ENTER}\nVarA=hello\nPasteZ=world\nPoint1=10,20\nZoneZ=1,2,30,40\n").unwrap();
        assert_eq!(v["vars"]["A"], "hello");
        assert_eq!(v["points"][0], json!([10, 20]));
        assert_eq!(v["zones"]["Z"], json!([1, 2, 30, 40]));
        assert_eq!(v["shells"]["P"], "pwsh");
        assert_eq!(v["shells"]["T"], "");
        assert!(v["script"].as_str().unwrap().contains('\n'));
        validate_state(&v).unwrap();
        assert!(import_legacy("nothing").is_err());
    }
    #[test]
    fn older_environments_gain_shell_settings_without_overwriting_commands() {
        let mut old = default_state();
        old["shells"].as_object_mut().unwrap().remove("P");
        old["shells"].as_object_mut().unwrap().remove("T");
        old.as_object_mut().unwrap().remove("paths");
        old["view"].as_object_mut().unwrap().remove("panel");
        old["shells"]["X"] = json!("fish -l");
        let file = std::env::temp_dir().join(format!(
            "macrohelp-shells-{}-{}.json",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::write(&file, old.to_string()).unwrap();
        let state = read_state(&file).unwrap();
        fs::remove_file(file).unwrap();
        assert_eq!(state["shells"]["X"], "fish -l");
        assert_eq!(state["shells"]["P"], "pwsh");
        assert_eq!(state["shells"]["T"], "");
        assert_eq!(state["paths"], json!({"import":"","export":""}));
        assert_eq!(state["view"]["panel"], true);
        old["shells"]["P"] = json!(12);
        assert!(validate_state(&old).is_err());
    }
    #[test]
    fn rejects_invalid_coordinates() {
        let mut v = default_state();
        v["points"][0] = json!(["bad", 2]);
        assert!(validate_state(&v).is_err());
        let mut v = default_state();
        v["zones"] = json!([]);
        assert!(validate_state(&v).is_err());
        let mut v = default_state();
        v["zones"].as_object_mut().unwrap().remove("Z");
        assert!(validate_state(&v).is_err());
    }
}
