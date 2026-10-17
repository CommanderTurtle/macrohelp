//! Counterpart of http-daemon/include/Types.h.
use serde_json::{Value, json};
pub type Result<T> = std::result::Result<T, ApiError>;
#[derive(Debug, Clone)]
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
impl std::fmt::Display for ApiError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}
impl std::error::Error for ApiError {}
pub fn ok(message: impl Into<String>, data: Value) -> Value {
    let mut v = json!({"success":true,"message":message.into()});
    if !data.is_null() {
        v["data"] = data;
    }
    v
}
pub fn failure(error: &ApiError) -> Value {
    json!({"success":false,"message":error.message})
}
pub fn string<'a>(v: &'a Value, key: &str) -> Result<&'a str> {
    v[key]
        .as_str()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| ApiError::new(400, format!("Missing non-empty string: {key}")))
}
pub fn integer(v: &Value, default: i64, min: i64, max: i64) -> Result<i64> {
    let n = if v.is_null() {
        default
    } else if let Some(s) = v.as_str() {
        if ["inf", "infinite"].contains(&s.to_ascii_lowercase().as_str()) {
            -1
        } else {
            s.parse()
                .map_err(|_| ApiError::new(400, "Expected an integer"))?
        }
    } else {
        v.as_i64()
            .ok_or_else(|| ApiError::new(400, "Expected an integer"))?
    };
    if n < min || n > max {
        return Err(ApiError::new(400, format!("Integer must be {min}–{max}")));
    }
    Ok(n)
}
pub fn name(raw: &str) -> Result<String> {
    let raw = raw.trim();
    let lower = raw.to_ascii_lowercase();
    let s = if lower.ends_with(".omscht") {
        &raw[..raw.len() - 7]
    } else if lower.ends_with(".scht") {
        &raw[..raw.len() - 5]
    } else {
        raw
    };
    if s.is_empty()
        || s.len() > 120
        || s == "."
        || s == ".."
        || s.chars()
            .any(|c| c.is_control() || "/\\:\"<>|?*".contains(c))
    {
        return Err(ApiError::new(400, "Invalid schedule name"));
    }
    Ok(s.into())
}
pub fn timestamp(ms: Option<u64>) -> String {
    ms.and_then(|ms| time::OffsetDateTime::from_unix_timestamp_nanos(ms as i128 * 1_000_000).ok())
        .and_then(|t| {
            t.format(&time::format_description::well_known::Rfc3339)
                .ok()
        })
        .unwrap_or_default()
}
pub fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}
