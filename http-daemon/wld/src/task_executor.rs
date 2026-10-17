//! TaskExecutor counterpart: delegate to Omarsket, never synthesize input here.
use crate::types::{ApiError, Result};
use serde_json::{Value, json};
use std::path::PathBuf;
pub trait Executor: Send + Sync {
    fn call(&self, request: Value) -> Result<Value>;
}
pub struct SocketExecutor {
    pub path: PathBuf,
}
impl Executor for SocketExecutor {
    fn call(&self, request: Value) -> Result<Value> {
        #[cfg(unix)]
        {
            use std::{
                io::{BufRead, BufReader, Read, Write},
                os::unix::net::UnixStream,
                time::Duration,
            };
            let mut stream=UnixStream::connect(&self.path).map_err(|e|ApiError::new(503,format!("Omarsket control socket unavailable: {e}. Update/initialize and enable the existing Omarsket plugin.")))?;
            stream
                .set_read_timeout(Some(Duration::from_secs(5)))
                .map_err(io_error)?;
            stream
                .set_write_timeout(Some(Duration::from_secs(5)))
                .map_err(io_error)?;
            writeln!(stream, "{request}").map_err(io_error)?;
            let mut line = String::new();
            BufReader::new(stream)
                .take(16 * 1024 * 1024 + 1)
                .read_line(&mut line)
                .map_err(io_error)?;
            if line.len() > 16 * 1024 * 1024 {
                return Err(ApiError::new(502, "Omarsket response too large"));
            }
            decode(serde_json::from_str(&line).map_err(|e| ApiError::new(502, e.to_string()))?)
        }
        #[cfg(not(unix))]
        {
            let _ = request;
            Err(ApiError::new(503, "Live Omarsket control requires Unix"))
        }
    }
}
#[cfg(unix)]
fn io_error(e: std::io::Error) -> ApiError {
    ApiError::new(503, e.to_string())
}
pub fn decode(reply: Value) -> Result<Value> {
    if reply["ok"] == true {
        return Ok(reply["result"].clone());
    }
    let error = reply["error"]
        .as_str()
        .unwrap_or("Invalid Omarsket response");
    let status = if error.contains("not found") {
        404
    } else if error.contains("permission") {
        403
    } else if error.contains("exists") || error.contains("changed on disk") {
        409
    } else {
        400
    };
    Err(ApiError::new(status, error))
}
pub fn snapshot(executor: &dyn Executor) -> Result<Value> {
    executor.call(json!({"method":"snapshot"}))
}
