//! Local exports only. No execution or live-input recording.
use crate::client::{ApiError, Result};
use serde_json::{Value, json};
use std::{
    fs,
    io::Write,
    path::Path,
    sync::atomic::{AtomicU64, Ordering},
};
static SERIAL: AtomicU64 = AtomicU64::new(0);
fn bad(message: &str) -> ApiError {
    ApiError::new(400, message)
}
pub fn append_session(r: &Value) -> Result<Value> {
    let home = std::env::var_os("HOME")
        .map(std::path::PathBuf::from)
        .ok_or_else(|| bad("HOME is not configured"))?;
    let config = std::env::var_os("XDG_CONFIG_HOME")
        .map(std::path::PathBuf::from)
        .filter(|p| p.is_absolute())
        .unwrap_or_else(|| home.join(".config"));
    let download = fs::read_to_string(config.join("user-dirs.dirs"))
        .ok()
        .and_then(|text| {
            text.lines().find_map(|line| {
                let raw = line
                    .trim()
                    .strip_prefix("XDG_DOWNLOAD_DIR=")?
                    .trim()
                    .strip_prefix('"')?
                    .strip_suffix('"')?;
                let value = raw
                    .strip_prefix("$HOME/")
                    .map(|p| home.join(p))
                    .unwrap_or_else(|| std::path::PathBuf::from(raw));
                value.is_absolute().then_some(value)
            })
        })
        .unwrap_or_else(|| home.join("Downloads"));
    let mut request = r.clone();
    request["path"] = json!(download.join("clicksession.txt"));
    request["append"] = json!(true);
    actions(&request)
}
pub fn write(path: &Path, text: &str, expected: Option<&str>) -> Result<()> {
    if !path.is_absolute() || text.len() > 2 * 1024 * 1024 {
        return Err(bad("Choose an absolute export path, at most 2 MiB"));
    }
    let parent = path
        .parent()
        .ok_or_else(|| bad("Missing export directory"))?;
    if !parent.is_dir() {
        return Err(bad("Export directory does not exist"));
    }
    if let Ok(meta) = fs::symlink_metadata(path) {
        if !meta.is_file() || meta.len() > 2 * 1024 * 1024 {
            return Err(bad("Export target must be a regular file of at most 2 MiB"));
        }
        if expected.is_none() || fs::read_to_string(path).ok().as_deref() != expected {
            return Err(bad("File exists or changed outside Macrohelp"));
        }
    } else if expected.is_some() {
        return Err(bad("Export file disappeared"));
    }
    let temp = path.with_extension(format!(
        "{}.{}.tmp",
        std::process::id(),
        SERIAL.fetch_add(1, Ordering::Relaxed)
    ));
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut out = options.open(&temp).map_err(|e| bad(&e.to_string()))?;
    let result = (|| -> std::io::Result<()> {
        out.write_all(text.as_bytes())?;
        out.sync_all()?;
        drop(out);
        // Recheck before replacement; never discard a visible outside edit.
        if expected.is_some() && fs::read_to_string(path).ok().as_deref() != expected {
            return Err(std::io::Error::other("File changed outside Macrohelp"));
        }
        if expected.is_none() {
            fs::hard_link(&temp, path)?;
            fs::remove_file(&temp)
        } else {
            fs::rename(&temp, path)
        }
    })();
    if result.is_err() {
        let _ = fs::remove_file(temp);
    }
    result.map_err(|e| bad(&e.to_string()))
}
pub fn actions(r: &Value) -> Result<Value> {
    let path = Path::new(
        r["path"]
            .as_str()
            .ok_or_else(|| bad("Choose an export file"))?,
    );
    let input = r["actions"]
        .as_array()
        .filter(|a| !a.is_empty() && a.len() <= 10000)
        .ok_or_else(|| bad("Choose 1–10000 actions"))?;
    for action in input {
        if !matches!(
            action["type"].as_str(),
            Some(
                "paste"
                    | "wait"
                    | "keyssequence"
                    | "cursormovements"
                    | "runningothertask"
                    | "type"
                    | "scroll"
            )
        ) {
            return Err(bad("Unsupported action export"));
        }
    }
    let before = if r["append"] == true && path.exists() {
        let meta = fs::symlink_metadata(path).map_err(|e| bad(&e.to_string()))?;
        if !meta.is_file() || meta.len() > 2 * 1024 * 1024 {
            return Err(bad("Invalid append target"));
        }
        Some(fs::read_to_string(path).map_err(|e| bad(&e.to_string()))?)
    } else {
        None
    };
    let mut doc = if let Some(text) = &before {
        serde_json::from_str::<Value>(text)
            .map_err(|_| bad("Append target must be an action JSON document"))?
    } else {
        json!({"docType":"ScheduleTask File","description":"Macrohelp actions","actions":[]})
    };
    let list = doc
        .get_mut("actions")
        .and_then(Value::as_array_mut)
        .ok_or_else(|| bad("Missing actions array"))?;
    if list.len() + input.len() > 10000 {
        return Err(bad("Export exceeds 10000 actions"));
    }
    list.extend(input.iter().cloned());
    write(
        path,
        &serde_json::to_string_pretty(&doc).unwrap(),
        before.as_deref(),
    )?;
    Ok(json!({"path":path,"count":doc["actions"].as_array().unwrap().len()}))
}
fn escape(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('\r', "\\r")
        .replace('\n', "\\n")
        .replace('\t', "\\t")
}
pub fn legacy(state: &Value) -> String {
    let mut out = String::new();
    let mut line = |key: &str, value: String| {
        out.push_str(key);
        out.push('=');
        out.push_str(&escape(&value));
        out.push('\n');
    };
    line("MacrohelpRegistryHubExportVersion", "1".into());
    for (key, field) in [("RegistryFilePath", "registry"), ("Script", "script")] {
        line(key, state[field].as_str().unwrap_or("").into());
    }
    for key in ["X", "C", "G", "P", "T"] {
        line(
            &format!("ShellAlias{key}"),
            state["shells"][key].as_str().unwrap_or("").into(),
        );
    }
    for key in ["import", "export"] {
        line(
            if key == "import" {
                "ImportPath"
            } else {
                "ExportPath"
            },
            state["paths"][key].as_str().unwrap_or("").into(),
        );
    }
    for (field, prefix, keys) in [
        ("vars", "Var", &["A", "S", "D", "F"][..]),
        ("buffers", "Paste", &["Z", "X", "C", "V"][..]),
    ] {
        for key in keys {
            line(
                &format!("{prefix}{key}"),
                state[field][key].as_str().unwrap_or("").into(),
            );
        }
    }
    for (field, prefix, keys) in [
        (
            "zones",
            "Zone",
            vec!["Z".into(), "X".into(), "C".into(), "V".into()],
        ),
        (
            "points",
            "Point",
            (1..=16).map(|n| n.to_string()).collect::<Vec<_>>(),
        ),
    ] {
        for (i, key) in keys.iter().enumerate() {
            let value = if field == "points" {
                &state[field][i]
            } else {
                &state[field][key]
            };
            line(
                &format!("{prefix}{key}Set"),
                if value.is_null() { "0" } else { "1" }.into(),
            );
            line(
                &format!("{prefix}{key}"),
                value
                    .as_array()
                    .map(|a| a.iter().map(Value::to_string).collect::<Vec<_>>().join(","))
                    .unwrap_or_default(),
            );
        }
    }
    for (key, field) in [
        ("CrosshairVisible", "crosshair"),
        ("CoordinatePanelVisible", "panel"),
        ("CursorCoordinatesVisible", "coordinates"),
        ("KeyHudVisible", "keys"),
        ("GridVisible", "grid"),
        ("CoordinatesOnlyMode", "coordinates_only"),
    ] {
        line(
            key,
            if state["view"][field] == true {
                "1"
            } else {
                "0"
            }
            .into(),
        );
    }
    line(
        "LastTasketTaskNumber",
        state["last_task"]["task_number"]
            .as_u64()
            .unwrap_or(0)
            .to_string(),
    );
    line(
        "LastTasketTaskState",
        state["last_task"]["state"].as_str().unwrap_or("").into(),
    );
    line(
        "LastTasketTaskMessage",
        state["last_task"]["message"].as_str().unwrap_or("").into(),
    );
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn action_exports_append_without_clobbering_other_files() {
        let dir = std::env::temp_dir().join(format!(
            "macrohelp-actions-{}-{}",
            std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&dir).unwrap();
        let path = dir.join("keys.json");
        let request = json!({"path":path,"actions":[{"type":"wait","duration":0.2}]});
        assert_eq!(actions(&request).unwrap()["count"], 1);
        assert!(actions(&request).is_err());
        let mut append = request.clone();
        append["append"] = json!(true);
        assert_eq!(actions(&append).unwrap()["count"], 2);
        let text = fs::read_to_string(&path).unwrap();
        assert!(write(&path, "changed", Some("stale")).is_err());
        assert_eq!(fs::read_to_string(&path).unwrap(), text);
        fs::write(&path, "not JSON").unwrap();
        assert!(actions(&append).is_err());
        assert_eq!(fs::read_to_string(&path).unwrap(), "not JSON");
        fs::write(&path, "[]").unwrap();
        assert!(actions(&append).is_err());
        assert!(write(Path::new("relative.json"), "x", None).is_err());
        assert!(actions(&json!({"path":path,"actions":[{"type":"unknown"}]})).is_err());
        fs::remove_file(&path).unwrap();
        fs::remove_dir(&dir).unwrap();
    }
    #[test]
    fn windows_export_preserves_environment_and_escape_sequences() {
        let mut state = crate::overlay::default_state();
        state["script"] = json!("{type 🐢}\n{wait 100}\t\\n\r");
        state["points"][3] = json!([-12, 100]);
        state["zones"]["C"] = json!([1, 2, 30, 40]);
        state["shells"]["P"] = json!("pwsh -NoLogo");
        state["view"]["panel"] = json!(false);
        state["last_task"] = json!({"task_number":17,"state":"finished","message":"okay"});
        state["paths"]["export"] = json!("/tmp/environment.txt");
        let restored = crate::overlay::import_legacy(&legacy(&state)).unwrap();
        assert_eq!(restored, state);
        let reverse = "MacrohelpRegistryHubExportVersion=1\nLastTasketTaskState=finished\nLastTasketTaskMessage=okay\nLastTasketTaskNumber=17\n";
        assert_eq!(
            crate::overlay::import_legacy(reverse).unwrap()["last_task"],
            state["last_task"]
        );
    }
}
