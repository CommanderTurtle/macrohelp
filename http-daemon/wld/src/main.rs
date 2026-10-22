use macrohelp_wayland::{
    config::Config,
    http_server::{App, serve},
    task_executor::SocketExecutor,
    task_runner::Runner,
};
use serde_json::{json, Value};
use std::{
    path::PathBuf,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
};
static STOP: AtomicBool = AtomicBool::new(false);
#[cfg(unix)]
extern "C" fn interrupted(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}
fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(1)
    }
}
fn run() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help") {
        println!(
            "macrohelp-httpd [--env /path/to/.env]\nmacrohelp-httpd --init [--env PATH] [--omarsket PATH]\nForeground communicator for the existing Omarsket backend. Ctrl+C stops HTTP-owned runs only."
        );
        return Ok(());
    }
    if args.first().is_some_and(|a| a == "--init") {
        let mut path = macrohelp_wayland::configure::default_env();
        let mut install = None;
        let mut i = 1;
        while i < args.len() {
            let value = args.get(i + 1).ok_or("Missing option value")?;
            match args[i].as_str() {
                "--env" => path = PathBuf::from(value),
                "--omarsket" => install = Some(value.as_str()),
                _ => return Err("Expected --env PATH or --omarsket PATH".into()),
            }
            i += 2;
        }
        macrohelp_wayland::configure::initialize(&path, install)?;
        return Ok(());
    }
    let path = if args.is_empty() {
        macrohelp_wayland::configure::default_env()
    } else if args.len() == 2 && args[0] == "--env" {
        PathBuf::from(&args[1])
    } else {
        return Err("Expected --env PATH".into());
    };
    let config = Config::load(&path)?;
    let executor = Arc::new(SocketExecutor {
        path: config.socket.clone(),
    });
    let app = Arc::new(App::new(Runner::new(config.clone(), executor)));
    app.runner.snapshot()?;
    let _blink = ensure_blink_task(&app);
    let server = Arc::new(std::net::TcpListener::bind(config.address)?);
    eprintln!(
        "Macrohelp HTTP: http://{} · shared Omarsket queue · authentication required",
        config.address
    );
    #[cfg(unix)]
    unsafe {
        libc::signal(libc::SIGINT, interrupted as *const () as usize);
        libc::signal(libc::SIGTERM, interrupted as *const () as usize);
    }
    let stop = Arc::new(AtomicBool::new(false));
    let signal = stop.clone();
    std::thread::spawn(move || {
        while !STOP.load(Ordering::Relaxed) {
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        signal.store(true, Ordering::Relaxed);
    });
    serve(server, app.clone(), stop);
    remove_blink_task(&app);
    Ok(())
}

/// Ensures the "Blink to center bottom" task exists. Creates it if missing.
fn ensure_blink_task(app: &macrohelp_wayland::http_server::App) -> Option<String> {
    let snapshot = app.runner.snapshot().ok()?;
    let tasks = snapshot["tasks"].as_array()?;
    if let Some(existing) = tasks.iter().find(|t| {
        t["id"]
            .as_str()
            .is_some_and(|id| id.starts_with("Blink to ") && id.ends_with(" (center bottom)"))
    }) {
        return existing["id"].as_str().map(String::from);
    }
    let output = std::process::Command::new("hyprctl")
        .args(["monitors", "-j"])
        .output()
        .ok()?;
    let stdout = String::from_utf8(output.stdout).ok()?;
    let monitors: Vec<Value> = serde_json::from_str(&stdout).ok()?;
    let monitor = monitors.first()?;
    let width = monitor["width"].as_u64()?;
    let height = monitor["height"].as_u64()?;
    let cx = (width / 2) as i64;
    let cy = (height - 36) as i64;
    let id = format!("Blink to {}x{} (center bottom)", cx, cy);
    let task = json!({
        "actions": [{"duration_ms": 20, "type": "move", "x": cx, "y": cy}],
        "allow_system": false,
        "auto_start": false,
        "delay_seconds": 0,
        "description": format!("Moves cursor instantly to center bottom of {}x{} display.", width, height),
        "id": id,
        "loop_times": 1
    });
    app.runner
        .executor
        .call(json!({"method":"save","task":task,"create":true}))
        .ok()?;
    Some(id)
}

/// Removes the "Blink to center bottom" task if present.
fn remove_blink_task(app: &macrohelp_wayland::http_server::App) {
    let Ok(snapshot) = app.runner.snapshot() else { return };
    let Some(tasks) = snapshot["tasks"].as_array() else { return };
    let Some(task) = tasks.iter().find(|t| {
        t["id"]
            .as_str()
            .is_some_and(|id| id.starts_with("Blink to ") && id.ends_with(" (center bottom)"))
    }) else {
        return
    };
    let id = task["id"].as_str().unwrap_or("");
    let _ = app.runner.executor.call(json!({"method":"delete","name":id}));
}
