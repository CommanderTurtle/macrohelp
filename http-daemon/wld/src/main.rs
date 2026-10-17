use macrohelp_wayland::{
    config::Config,
    http_server::{App, serve},
    task_executor::SocketExecutor,
    task_runner::Runner,
};
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
    serve(server, app, stop);
    Ok(())
}
