//! Partial Macrohelp environments, separate from schedule files and key history.
use crate::{
    client::{ApiError, Result},
    overlay::{default_state, validate_state},
};
use serde_json::{Value, json};
use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
};
static TEMP_ID: AtomicU64 = AtomicU64::new(0);

const SECTIONS: [&str; 8] = [
    "script", "registry", "shells", "vars", "buffers", "zones", "points", "view",
];
fn invalid(message: &str) -> ApiError {
    ApiError::new(400, message)
}
fn path(root: &Path, name: &str) -> Result<PathBuf> {
    if name.is_empty()
        || name.chars().count() > 120
        || name.trim() != name
        || name.contains(['/', '\\', '{', '}', ':'])
        || name.chars().any(char::is_control)
        || matches!(name, "." | "..")
    {
        return Err(invalid("Invalid preset name"));
    }
    Ok(root.join(format!("{name}.mhprofile.json")))
}
pub fn merge(base: &Value, data: &Value) -> Result<Value> {
    let mut out = base.clone();
    let defaults = default_state();
    for (section, value) in data
        .as_object()
        .ok_or_else(|| invalid("Invalid preset data"))?
    {
        if !SECTIONS.contains(&section.as_str()) {
            return Err(invalid("Unknown preset section"));
        }
        if ["vars", "buffers", "shells", "zones", "view"].contains(&section.as_str()) {
            for (key, value) in value
                .as_object()
                .ok_or_else(|| invalid("Invalid preset section"))?
            {
                if defaults[section].get(key).is_none() {
                    return Err(invalid("Unknown preset setting"));
                }
                out[section][key] = value.clone();
            }
        } else {
            out[section] = value.clone();
        }
    }
    validate_state(&out)?;
    Ok(out)
}
fn validate(value: &Value) -> Result<()> {
    if value["version"] != 1 {
        return Err(invalid("Invalid Macrohelp preset"));
    }
    merge(&default_state(), &value["data"])?;
    Ok(())
}
fn read(file: &Path) -> Result<Value> {
    let meta = fs::symlink_metadata(file).map_err(|e| invalid(&e.to_string()))?;
    if !meta.is_file() || meta.len() > 1024 * 1024 {
        return Err(invalid("Preset must be a regular file of at most 1 MiB"));
    }
    let value = serde_json::from_slice(&fs::read(file).map_err(|e| invalid(&e.to_string()))?)
        .map_err(|_| invalid("Invalid preset JSON"))?;
    validate(&value)?;
    Ok(value)
}
pub fn list(root: &Path) -> Result<Value> {
    let mut presets = serde_json::Map::new();
    let mut total = 0;
    if root.exists() {
        for entry in fs::read_dir(root).map_err(|e| invalid(&e.to_string()))? {
            let file = entry.map_err(|e| invalid(&e.to_string()))?.path();
            let Some(name) = file
                .file_name()
                .and_then(|s| s.to_str())
                .and_then(|s| s.strip_suffix(".mhprofile.json"))
            else {
                continue;
            };
            path(root, name)?;
            let value = read(&file)?;
            total += value.to_string().len();
            if total > 16 * 1024 * 1024 {
                return Err(invalid("Preset library exceeds 16 MiB"));
            }
            presets.insert(name.into(), value["data"].clone());
        }
    }
    Ok(json!({"directory":root,"presets":presets}))
}
pub fn save(root: &Path, request: &Value) -> Result<Value> {
    let name = request["name"]
        .as_str()
        .ok_or_else(|| invalid("Enter a preset name"))?;
    let file = path(root, name)?;
    validate_state(&request["state"])?;
    let mut data = json!({});
    for field in request["fields"]
        .as_array()
        .ok_or_else(|| invalid("Choose preset fields"))?
    {
        let field = field
            .as_str()
            .ok_or_else(|| invalid("Invalid preset field"))?;
        let (section, key) = field.split_once('.').unwrap_or((field, ""));
        if !SECTIONS.contains(&section) {
            return Err(invalid("Unknown preset section"));
        }
        if key.is_empty() {
            data[section] = request["state"][section].clone();
        } else if ["vars", "buffers", "shells", "zones", "view"].contains(&section)
            && request["state"][section].get(key).is_some()
        {
            data[section][key] = request["state"][section][key].clone();
        } else {
            return Err(invalid("Unknown preset field"));
        }
    }
    if data.as_object().unwrap().is_empty() {
        return Err(invalid("Choose at least one preset field"));
    }
    let value = json!({"version":1,"data":data});
    validate(&value)?;
    let bytes = serde_json::to_vec_pretty(&value).map_err(|e| invalid(&e.to_string()))?;
    if bytes.len() > 1024 * 1024 {
        return Err(invalid("Preset exceeds 1 MiB"));
    }
    fs::create_dir_all(root).map_err(|e| invalid(&e.to_string()))?;
    let meta = fs::symlink_metadata(root).map_err(|e| invalid(&e.to_string()))?;
    if !meta.is_dir() {
        return Err(invalid("Preset directory must be a real directory"));
    }
    if file.exists() {
        let meta = fs::symlink_metadata(&file).map_err(|e| invalid(&e.to_string()))?;
        if !meta.is_file() || request["replace"] != true {
            return Err(invalid("Confirm replacing this preset"));
        }
    }
    let temp = file.with_extension(format!(
        "{}.{}.tmp",
        std::process::id(),
        TEMP_ID.fetch_add(1, Ordering::Relaxed)
    ));
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::{MetadataExt, OpenOptionsExt, PermissionsExt};
        if meta.uid() != unsafe { libc::geteuid() } {
            return Err(invalid("Preset directory belongs to another user"));
        }
        fs::set_permissions(root, fs::Permissions::from_mode(0o700))
            .map_err(|e| invalid(&e.to_string()))?;
        options.mode(0o600);
    }
    let mut created = false;
    let result = (|| -> std::io::Result<()> {
        let mut output = options.open(&temp)?;
        created = true;
        output.write_all(&bytes)?;
        output.sync_all()?;
        drop(output);
        fs::rename(&temp, &file)
    })();
    if result.is_err() && created {
        let _ = fs::remove_file(temp);
    }
    result.map_err(|e| invalid(&e.to_string()))?;
    list(root)
}
pub fn import(root: &Path, name: &str) -> Result<Value> {
    merge(&default_state(), &read(&path(root, name)?)?["data"])
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_fields_merge_but_literal_import_resets() {
        let mut state = default_state();
        state["vars"]["A"] = json!("existing");
        state["vars"]["S"] = json!("keep");
        let data = json!({"script":"{paste A}","vars":{"A":"preset"}});
        assert_eq!(merge(&state, &data).unwrap()["vars"]["S"], "keep");
        assert_eq!(merge(&default_state(), &data).unwrap()["vars"]["S"], "");
        assert!(merge(&state, &json!({"events":[1]})).is_err());
        assert!(path(Path::new("profiles"), "../escape").is_err());
    }
    #[test]
    fn selected_settings_roundtrip_without_exporting_unselected_values() {
        let root = std::env::temp_dir().join(format!(
            "macrohelp-presets-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let mut state = default_state();
        state["vars"]["A"] = json!("selected");
        state["vars"]["S"] = json!("not exported");
        state["script"] = json!("{paste A}");
        save(
            &root,
            &json!({"name":"1 - Test","state":state,"fields":["script","vars.A"]}),
        )
        .unwrap();
        let book = list(&root).unwrap();
        assert!(book["presets"]["1 - Test"]["vars"].get("S").is_none());
        assert_eq!(import(&root, "1 - Test").unwrap()["vars"]["S"], "");
        assert!(
            save(
                &root,
                &json!({"name":"1 - Test","state":state,"fields":["script"]})
            )
            .is_err()
        );
        fs::remove_file(path(&root, "1 - Test").unwrap()).unwrap();
        fs::remove_dir(root).unwrap();
    }
}
