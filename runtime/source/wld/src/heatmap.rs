//! Named aggregate-only sessions. No text, event sequence or event timestamps.
use crate::client::{ApiError, Result};
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, io::Write, path::Path};

fn invalid(message: &str) -> ApiError {
    ApiError::new(400, message)
}
pub fn empty() -> Value {
    json!({"version":1,"active":null,"sessions":[]})
}
pub fn load(path: &Path) -> Result<Value> {
    if !path.exists() {
        return Ok(empty());
    }
    let meta = fs::symlink_metadata(path).map_err(|e| invalid(&e.to_string()))?;
    if !meta.is_file() || meta.len() > 16 * 1024 * 1024 {
        return Err(invalid("Invalid heatmap session file"));
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        if meta.uid() != unsafe { libc::geteuid() } || meta.mode() & 0o077 != 0 {
            return Err(invalid("Heatmap sessions must be private (chmod 600)"));
        }
    }
    let value: Value =
        serde_json::from_slice(&fs::read(path).map_err(|e| invalid(&e.to_string()))?)
            .map_err(|e| invalid(&e.to_string()))?;
    validate(&value)?;
    Ok(value)
}
fn validate(value: &Value) -> Result<()> {
    if value["version"] != 1 || !value["sessions"].is_array() {
        return Err(invalid("Invalid heatmap sessions"));
    }
    let mut ids = BTreeSet::new();
    for session in value["sessions"].as_array().unwrap() {
        if !session["id"].is_string()
            || !ids.insert(session["id"].as_str().unwrap())
            || session["name"]
                .as_str()
                .is_none_or(|s| s.trim().is_empty() || s.chars().count() > 120)
            || !session["started"].is_u64()
            || !(session["ended"].is_null() || session["ended"].is_u64())
            || session["values"].as_object().is_none_or(|m| {
                m.len() > 768
                    || m.iter().any(|(k, v)| {
                        !key_code(k) || v.as_f64().is_none_or(|n| !(0.0..=1.0).contains(&n))
                    })
            })
            || session["weight"]
                .as_f64()
                .is_none_or(|n| !n.is_finite() || n < 0.0)
            || !session["weights"].is_object()
            || !session["elapsed"].is_u64()
            || session["work"].as_u64().is_none_or(|n| n > 86400)
            || session["rest"].as_u64().is_none_or(|n| n > 86400)
        {
            return Err(invalid("Invalid heatmap aggregate"));
        }
        weights(&session["weights"])?;
    }
    if !value["active"].is_null()
        && !value["sessions"]
            .as_array()
            .unwrap()
            .iter()
            .any(|s| s["id"] == value["active"] && s["ended"].is_null())
    {
        return Err(invalid("Missing active heatmap session"));
    }
    Ok(())
}
pub fn save(path: &Path, value: &Value) -> Result<()> {
    validate(value)?;
    let bytes = serde_json::to_vec(value).map_err(|e| invalid(&e.to_string()))?;
    if bytes.len() > 16 * 1024 * 1024 {
        return Err(invalid("Heatmap sessions exceed 16 MiB"));
    }
    let parent = path
        .parent()
        .ok_or_else(|| invalid("Missing session directory"))?;
    fs::create_dir_all(parent).map_err(|e| invalid(&e.to_string()))?;
    let temp = path.with_extension(format!("{}.tmp", std::process::id()));
    let mut options = fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
        if fs::symlink_metadata(parent)
            .map_err(|e| invalid(&e.to_string()))?
            .file_type()
            .is_symlink()
        {
            return Err(invalid("Session directory may not be a symlink"));
        }
        fs::set_permissions(parent, fs::Permissions::from_mode(0o700))
            .map_err(|e| invalid(&e.to_string()))?;
        options.mode(0o600);
    }
    let mut file = options.open(&temp).map_err(|e| invalid(&e.to_string()))?;
    let result = (|| {
        file.write_all(&bytes)?;
        file.sync_all()?;
        drop(file);
        #[cfg(windows)]
        if path.exists() {
            fs::remove_file(path)?;
        }
        fs::rename(&temp, path)
    })();
    if result.is_err() {
        let _ = fs::remove_file(temp);
    }
    result.map_err(|e| invalid(&e.to_string()))
}
fn weights(value: &Value) -> Result<()> {
    let Some(map) = value.as_object() else {
        return Err(invalid("Invalid key weights"));
    };
    if map.len() > 768
        || map
            .iter()
            .any(|(k, v)| !key_code(k) || v.as_f64().is_none_or(|n| !(0.01..=100.0).contains(&n)))
    {
        return Err(invalid("Key weights must be between 0.01 and 100"));
    }
    Ok(())
}
fn key_code(code: &str) -> bool {
    code.parse::<u16>()
        .is_ok_and(|n| n < 768 && n.to_string() == code)
}
pub fn current(value: &Value) -> Option<&Value> {
    value["sessions"]
        .as_array()?
        .iter()
        .find(|s| s["id"] == value["active"])
}
pub fn active(value: &Value) -> bool {
    !value["active"].is_null()
}
fn selected(value: &mut Value) -> Option<&mut Value> {
    let id = value["active"].clone();
    value["sessions"]
        .as_array_mut()?
        .iter_mut()
        .find(|s| s["id"] == id)
}
pub fn collecting(value: &Value) -> bool {
    let Some(session) = value["sessions"]
        .as_array()
        .and_then(|s| s.iter().find(|s| s["id"] == value["active"]))
    else {
        return false;
    };
    let work = session["work"].as_u64().unwrap_or(0);
    let rest = session["rest"].as_u64().unwrap_or(0);
    work == 0 || rest == 0 || session["elapsed"].as_u64().unwrap_or(0) % (work + rest) < work
}
pub fn tick(value: &mut Value, seconds: u64) {
    if let Some(session) = selected(value) {
        session["elapsed"] = json!(
            session["elapsed"]
                .as_u64()
                .unwrap_or(0)
                .saturating_add(seconds)
        );
    }
}
pub fn add(value: &mut Value, counts: &Value) {
    if !collecting(value) {
        return;
    }
    let Some(session) = selected(value) else {
        return;
    };
    let Some(counts) = counts.as_object() else {
        return;
    };
    let mut deltas = Vec::new();
    for (code, count) in counts {
        if !key_code(code) {
            continue;
        }
        let Some(n) = count.as_u64().filter(|n| *n <= 1_000_000) else {
            continue;
        };
        let weight = session["weights"][code].as_f64().unwrap_or(1.0) * n as f64;
        if weight > 0.0 {
            deltas.push((code.clone(), weight));
        }
    }
    let old = session["weight"].as_f64().unwrap_or(0.0);
    let total = old + deltas.iter().map(|(_, w)| w).sum::<f64>();
    if total <= old {
        return;
    }
    let Some(values) = session["values"].as_object_mut() else {
        return;
    };
    for value in values.values_mut() {
        *value = json!(value.as_f64().unwrap_or(0.0) * old / total)
    }
    for (code, weight) in deltas {
        let old = values.get(&code).and_then(Value::as_f64).unwrap_or(0.0);
        values.insert(code, json!(old + weight / total));
    }
    session["weight"] = json!(total);
}
pub fn command(value: &mut Value, request: &Value, now: u64) -> Result<()> {
    match request["action"].as_str().unwrap_or("snapshot") {
        "snapshot" => (),
        "start" => {
            let name = request["name"].as_str().unwrap_or("").trim();
            if name.is_empty() || name.chars().count() > 120 {
                return Err(invalid("Enter a session name (1–120 characters)"));
            }
            let key_weights = request.get("weights").cloned().unwrap_or(json!({}));
            weights(&key_weights)?;
            let work = request["work"]
                .as_u64()
                .filter(|v| *v <= 86400)
                .ok_or_else(|| invalid("Invalid focus timer"))?;
            let rest = request["rest"]
                .as_u64()
                .filter(|v| *v <= 86400)
                .ok_or_else(|| invalid("Invalid break timer"))?;
            if let Some(previous) = selected(value) {
                previous["ended"] = json!(now);
            }
            let sessions = value["sessions"].as_array_mut().unwrap();
            let mut suffix = sessions.len();
            while sessions
                .iter()
                .any(|s| s["id"] == format!("{now}-{suffix}"))
            {
                suffix += 1;
            }
            let id = format!("{now}-{suffix}");
            sessions.push(json!({"id":id,"name":name,"started":now,"ended":null,"elapsed":0,"work":work,"rest":rest,"weights":key_weights,"values":{},"weight":0.0}));
            value["active"] = json!(id);
        }
        "resume" => {
            let id = request["id"]
                .as_str()
                .ok_or_else(|| invalid("Choose a session"))?;
            if !value["sessions"]
                .as_array()
                .unwrap()
                .iter()
                .any(|s| s["id"] == id)
            {
                return Err(invalid("Session not found"));
            }
            if value["active"] != id
                && let Some(previous) = selected(value)
            {
                previous["ended"] = json!(now);
            }
            let session = value["sessions"]
                .as_array_mut()
                .unwrap()
                .iter_mut()
                .find(|s| s["id"] == id)
                .ok_or_else(|| invalid("Session not found"))?;
            session["ended"] = Value::Null;
            value["active"] = json!(id);
        }
        "end" => {
            if let Some(session) = selected(value) {
                session["ended"] = json!(now)
            }
            value["active"] = Value::Null
        }
        "delete" => {
            let id = &request["id"];
            if id == &value["active"] {
                return Err(invalid("End the active session before deleting it"));
            }
            value["sessions"]
                .as_array_mut()
                .unwrap()
                .retain(|s| &s["id"] != id);
        }
        _ => return Err(invalid("Unknown heatmap action")),
    }
    validate(value)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn weighted_average_is_normalized_and_restart_safe() {
        let mut v = empty();
        command(
            &mut v,
            &json!({"action":"start","name":"Writing","work":0,"rest":0,"weights":{"30":2}}),
            10,
        )
        .unwrap();
        add(&mut v, &json!({"30":2,"48":4}));
        assert_eq!(v["sessions"][0]["values"]["30"], 0.5);
        assert_eq!(v["sessions"][0]["values"]["48"], 0.5);
        let saved = serde_json::to_string(&v).unwrap();
        v = serde_json::from_str(&saved).unwrap();
        validate(&v).unwrap();
        add(&mut v, &json!({"30":4}));
        assert_eq!(v["sessions"][0]["values"]["30"], 0.75);
        assert_eq!(v["sessions"][0]["values"]["48"], 0.25);
        assert!(active(&v));
        assert!(!saved.contains("events"));
        assert!(!saved.contains("held"));
    }
    #[test]
    fn pomodoro_end_resume_and_named_sessions() {
        let mut v = empty();
        command(
            &mut v,
            &json!({"action":"start","name":"Overwatch","work":25,"rest":5}),
            10,
        )
        .unwrap();
        tick(&mut v, 25);
        assert!(!collecting(&v));
        add(&mut v, &json!({"30":20}));
        assert_eq!(v["sessions"][0]["weight"], 0.0);
        tick(&mut v, 5);
        assert!(collecting(&v));
        let id = v["active"].clone();
        command(&mut v, &json!({"action":"end"}), 40).unwrap();
        assert!(!active(&v));
        command(&mut v, &json!({"action":"resume","id":id}), 50).unwrap();
        assert!(active(&v));
        assert!(command(&mut v, &json!({"action":"delete","id":id}), 50).is_err());
        command(&mut v, &json!({"action":"end"}), 60).unwrap();
        command(
            &mut v,
            &json!({"action":"start","name":"Other","work":0,"rest":0}),
            70,
        )
        .unwrap();
        assert_eq!(v["sessions"].as_array().unwrap().len(), 2);
        command(&mut v, &json!({"action":"resume","id":id}), 80).unwrap();
        assert_eq!(v["sessions"][1]["ended"], 80);
        let mut broken = v.clone();
        broken["sessions"][0]["work"] = json!(u64::MAX);
        assert!(validate(&broken).is_err());
        assert!(!key_code("65535"));
        assert!(!key_code("030"));
        assert!(key_code("30"));
    }
}
