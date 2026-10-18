//! Overlay configuration and response types; no server or macro executor.
use serde_json::Value;
use std::{
    collections::BTreeMap,
    fmt, fs,
    net::{IpAddr, SocketAddr},
    path::{Path, PathBuf},
};

#[derive(Debug)]
pub struct ApiError {
    pub status: u16,
    pub message: String,
}
impl ApiError {
    pub fn new(status: u16, message: impl Into<String>) -> Self {
        Self {
            status,
            message: message.into(),
        }
    }
}
impl fmt::Display for ApiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}
impl std::error::Error for ApiError {}
pub type Result<T> = std::result::Result<T, ApiError>;
pub fn string<'a>(r: &'a Value, key: &str) -> Result<&'a str> {
    r[key]
        .as_str()
        .ok_or_else(|| ApiError::new(400, format!("Missing string: {key}")))
}
fn data_home() -> PathBuf {
    PathBuf::from(std::env::var_os("XDG_DATA_HOME").unwrap_or_else(|| {
        format!("{}/.local/share", std::env::var("HOME").unwrap_or_default()).into()
    }))
}
pub fn default_env() -> PathBuf {
    std::env::var_os("MACROHELP_ENV")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            fs::read_to_string(data_home().join("macrohelp/env-path"))
                .map(|s| PathBuf::from(s.trim()))
                .unwrap_or_else(|_| PathBuf::from(".env"))
        })
}
#[derive(Clone)]
pub struct Config {
    pub address: SocketAddr,
    pub key: String,
    pub env_file: PathBuf,
    pub state_dir: PathBuf,
}
impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let vars: BTreeMap<String, String> = if path.exists() {
            #[cfg(unix)]
            {
                use std::os::unix::fs::MetadataExt;
                let m =
                    fs::symlink_metadata(path).map_err(|e| ApiError::new(400, e.to_string()))?;
                if !m.is_file() || m.uid() != unsafe { libc::geteuid() } || m.mode() & 0o077 != 0 {
                    return Err(ApiError::new(400, "The .env must be private (chmod 600)"));
                }
            }
            dotenvy::from_path_iter(path)
                .map_err(|_| ApiError::new(400, "Cannot read overlay .env"))?
                .collect::<std::result::Result<_, _>>()
                .map_err(|_| ApiError::new(400, "Invalid .env syntax"))?
        } else {
            BTreeMap::new()
        };
        let get = |k: &str, d: &str| {
            std::env::var(k)
                .ok()
                .or_else(|| vars.get(k).cloned())
                .unwrap_or_else(|| d.into())
        };
        let ip: IpAddr = get("MACROHELP_BIND", "127.0.0.1")
            .parse()
            .map_err(|_| ApiError::new(400, "Invalid controller address"))?;
        if !ip.is_loopback() {
            return Err(ApiError::new(400, "The overlay only connects to loopback"));
        }
        let port: u16 = get("MACROHELP_PORT", "7777")
            .parse()
            .map_err(|_| ApiError::new(400, "Invalid controller port"))?;
        let key = get("MACROHELP_API_KEY", "");
        if !key.is_empty()
            && (key.len() < 24 || key.len() > 256 || !key.bytes().all(|b| (33..=126).contains(&b)))
        {
            return Err(ApiError::new(400, "Invalid controller key"));
        }
        let state_dir = PathBuf::from(get(
            "MACROHELP_STATE_DIR",
            &data_home().join("macrohelp").to_string_lossy(),
        ));
        Ok(Self {
            address: SocketAddr::new(ip, port),
            key,
            env_file: path.to_path_buf(),
            state_dir,
        })
    }
}
