use crate::{
    config::Config,
    types::{ApiError, Result},
};
use std::{
    fs,
    io::{self, IsTerminal, Read, Write},
    path::{Path, PathBuf},
};

pub fn data_dir() -> PathBuf {
    PathBuf::from(std::env::var_os("XDG_DATA_HOME").unwrap_or_else(|| {
        format!("{}/.local/share", std::env::var("HOME").unwrap_or_default()).into()
    }))
    .join("macrohelp")
}
pub fn default_env() -> PathBuf {
    std::env::var_os("MACROHELP_ENV")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            fs::read_to_string(data_dir().join("env-path"))
                .map(|s| PathBuf::from(s.trim()))
                .unwrap_or_else(|_| PathBuf::from(".env"))
        })
}
fn quote(s: &str) -> String {
    format!(
        "\"{}\"",
        s.replace('\\', "\\\\")
            .replace('"', "\\\"")
            .replace('$', "\\$")
            .replace('\n', "\\n")
    )
}
pub fn initialize(path: &Path, explicit: Option<&str>) -> Result<()> {
    let error = |e: io::Error| ApiError::new(400, e.to_string());
    if !path.exists() {
        let candidate = explicit
            .map(str::to_owned)
            .or_else(|| std::env::var("OMARSKET_INSTALL").ok())
            .unwrap_or_else(|| {
                format!(
                    "{}/.config/omarchy/plugins/io.github.commanderturtle.omarsket",
                    std::env::var("HOME").unwrap_or_default()
                )
            });
        let install = if explicit.is_some() || !io::stdin().is_terminal() {
            candidate
        } else {
            eprint!("Omarsket checkout [{candidate}]: ");
            io::stderr().flush().map_err(error)?;
            let mut line = String::new();
            io::stdin().read_line(&mut line).map_err(error)?;
            if line.trim().is_empty() {
                candidate
            } else {
                line.trim().to_owned()
            }
        };
        let m: serde_json::Value = serde_json::from_slice(
            &fs::read(Path::new(&install).join("manifest.json")).map_err(|_| {
                ApiError::new(
                    400,
                    "Specify the existing Omarsket checkout with --omarsket PATH",
                )
            })?,
        )
        .map_err(|e| ApiError::new(400, e.to_string()))?;
        if m["id"] != "io.github.commanderturtle.omarsket" || !Path::new(&install).is_absolute() {
            return Err(ApiError::new(400, "Not an absolute Omarsket plugin path"));
        }
        let runtime = std::env::var("XDG_RUNTIME_DIR").map_err(|_| {
            ApiError::new(
                400,
                "Run inside the user's Wayland login session (XDG_RUNTIME_DIR missing)",
            )
        })?;
        let mut random = [0u8; 32];
        fs::File::open("/dev/urandom")
            .and_then(|mut f| f.read_exact(&mut random))
            .map_err(error)?;
        let key: String = random.iter().map(|b| format!("{b:02x}")).collect();
        let content = format!(
            "OMARSKET_INSTALL={}\nOMARSKET_SOCKET={}\nMACROHELP_BIND=127.0.0.1\nMACROHELP_PORT=7777\nMACROHELP_API_KEY={}\nMACROHELP_DEFAULT_DELAY=1\nMACROHELP_ALLOW_SYSTEM=false\n",
            quote(&install),
            quote(&format!("{runtime}/omarsket/control.sock")),
            key
        );
        let mut opts = fs::OpenOptions::new();
        opts.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            opts.mode(0o600);
        }
        opts.open(path)
            .and_then(|mut f| f.write_all(content.as_bytes()))
            .map_err(error)?;
        eprintln!("Created {} (private API key generated).", path.display());
    }
    Config::load(path)?;
    let runtime = data_dir();
    fs::create_dir_all(&runtime).map_err(error)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::{MetadataExt, PermissionsExt};
        let meta = fs::symlink_metadata(&runtime).map_err(error)?;
        if !meta.is_dir() || meta.uid() != unsafe { libc::geteuid() } {
            return Err(ApiError::new(
                400,
                "Runtime directory must be owned by this user",
            ));
        }
        fs::set_permissions(&runtime, fs::Permissions::from_mode(0o700)).map_err(error)?;
    }
    let absolute = fs::canonicalize(path).map_err(error)?;
    fs::write(
        runtime.join("env-path"),
        absolute.to_string_lossy().as_bytes(),
    )
    .map_err(error)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn env_path_quoting_roundtrips_without_shell_evaluation() {
        for path in [
            "/home/a path/omarsket",
            "/tmp/dollar$secret/quote\"'",
            r"/tmp/a\b",
        ] {
            let line = format!("OMARSKET_INSTALL={}\n", quote(path));
            let entries: Vec<_> = dotenvy::from_read_iter(line.as_bytes()).collect();
            assert_eq!(entries.len(), 1);
            assert_eq!(entries[0].as_ref().unwrap().1, path);
        }
    }
}
