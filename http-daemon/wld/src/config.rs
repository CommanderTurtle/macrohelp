use crate::types::{ApiError, Result};
use std::{
    collections::BTreeMap,
    net::{IpAddr, SocketAddr},
    path::{Path, PathBuf},
};
#[derive(Clone)]
pub struct Config {
    pub install: PathBuf,
    pub socket: PathBuf,
    pub address: SocketAddr,
    pub key: String,
    pub default_delay: u64,
    pub allow_system: bool,
    pub cors: bool,
    pub env_file: PathBuf,
    pub state_dir: PathBuf,
}
impl Config {
    pub fn load(path: &Path) -> Result<Self> {
        let vars: BTreeMap<String, String> = dotenvy::from_path_iter(path)
            .map_err(|e| {
                ApiError::new(
                    400,
                    format!(
                        "{}: {e}; run bash http-daemon/wld/build.sh first",
                        path.display()
                    ),
                )
            })?
            .collect::<std::result::Result<_, _>>()
            .map_err(|_| ApiError::new(400, "Invalid .env syntax; values were not logged"))?;
        let get = |key: &str, default: &str| {
            std::env::var(key)
                .ok()
                .or_else(|| vars.get(key).cloned())
                .unwrap_or_else(|| default.into())
        };
        let install = PathBuf::from(get("OMARSKET_INSTALL", ""));
        if !install.is_absolute() {
            return Err(ApiError::new(400, "OMARSKET_INSTALL must be absolute"));
        }
        let manifest: serde_json::Value =
            serde_json::from_slice(&std::fs::read(install.join("manifest.json")).map_err(|e| {
                ApiError::new(
                    400,
                    format!(
                        "OMARSKET_INSTALL must point to an existing Omarsket plugin checkout: {e}"
                    ),
                )
            })?)
            .map_err(|e| ApiError::new(400, e.to_string()))?;
        if manifest["id"] != "io.github.commanderturtle.omarsket" {
            return Err(ApiError::new(
                400,
                "OMARSKET_INSTALL is not the Omarsket plugin",
            ));
        }
        let ip: IpAddr = get("MACROHELP_BIND", "127.0.0.1")
            .parse()
            .map_err(|_| ApiError::new(400, "Invalid MACROHELP_BIND"))?;
        if !ip.is_loopback() {
            return Err(ApiError::new(
                400,
                "This controller only binds loopback addresses",
            ));
        }
        let port: u16 = get("MACROHELP_PORT", "7777")
            .parse()
            .map_err(|_| ApiError::new(400, "Invalid MACROHELP_PORT"))?;
        let key = get("MACROHELP_API_KEY", "");
        if key.len() < 24 || key.len() > 256 || !key.bytes().all(|c| (33..=126).contains(&c)) {
            return Err(ApiError::new(
                400,
                "Set a MACROHELP_API_KEY of at least 24 characters",
            ));
        }
        if port == 0 {
            return Err(ApiError::new(400, "MACROHELP_PORT must be 1–65535"));
        }
        let socket = PathBuf::from(get(
            "OMARSKET_SOCKET",
            &format!(
                "{}/omarsket/control.sock",
                std::env::var("XDG_RUNTIME_DIR").unwrap_or_default()
            ),
        ));
        if !socket.is_absolute() || socket == Path::new("/omarsket/control.sock") {
            return Err(ApiError::new(400, "OMARSKET_SOCKET must be absolute"));
        }
        let default_delay: u64 = get("MACROHELP_DEFAULT_DELAY", "1")
            .parse()
            .map_err(|_| ApiError::new(400, "Invalid default delay"))?;
        if default_delay > 86400 {
            return Err(ApiError::new(400, "Default delay exceeds 24 hours"));
        }
        let home = std::env::var("HOME").unwrap_or_default();
        let state_dir = PathBuf::from(get(
            "MACROHELP_STATE_DIR",
            &format!(
                "{}/macrohelp",
                std::env::var("XDG_DATA_HOME").unwrap_or(format!("{home}/.local/share"))
            ),
        ));
        if !state_dir.is_absolute() {
            return Err(ApiError::new(400, "MACROHELP_STATE_DIR must be absolute"));
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            let meta =
                std::fs::symlink_metadata(path).map_err(|e| ApiError::new(400, e.to_string()))?;
            if !meta.is_file()
                || meta.uid() != unsafe { libc::geteuid() }
                || meta.mode() & 0o077 != 0
            {
                return Err(ApiError::new(
                    400,
                    "The .env must be a private, owned regular file (chmod 600)",
                ));
            }
        }
        Ok(Self {
            install,
            socket,
            address: SocketAddr::new(ip, port),
            key,
            default_delay,
            allow_system: get("MACROHELP_ALLOW_SYSTEM", "false") == "true",
            cors: get("MACROHELP_CORS", "false") == "true",
            env_file: path.to_path_buf(),
            state_dir,
        })
    }
}
