//! Saved references only; execution stays in the existing Registry Hub.
use crate::client::{ApiError, Result};
use serde_json::{Value, json};

fn name(value: &Value) -> Result<String> {
    let text = value.as_str().unwrap_or("").trim();
    if text.is_empty() || text.len() > 128 || text.bytes().any(|c| c < 32 || c == 127) {
        return Err(ApiError::new(
            400,
            "Skill names must be 1–128 bytes on one line",
        ));
    }
    Ok(text.into())
}
pub fn request(value: &Value, query: bool) -> Result<Value> {
    let object = value
        .as_object()
        .ok_or_else(|| ApiError::new(400, "Expected a skills object"))?;
    if object.keys().any(|k| k != "name" && (query || k != "rgd")) {
        return Err(ApiError::new(400, "Unknown skills field"));
    }
    let title = name(&value["name"])?;
    if query {
        return Ok(json!({"name":title}));
    }
    let reference = if title.eq_ignore_ascii_case("remove") {
        name(&value["rgd"])?
    } else {
        let text = value["rgd"]
            .as_str()
            .unwrap_or("")
            .trim()
            .to_ascii_lowercase();
        if text.len() > 128 || !crate::rgd::valid(&text) {
            return Err(ApiError::new(
                400,
                "Use an rgd reference such as i7s1 or i8z1",
            ));
        }
        text
    };
    Ok(json!({"name":title,"rgd":reference}))
}
pub fn validate(value: &Value) -> Result<()> {
    let map = value
        .as_object()
        .ok_or_else(|| ApiError::new(400, "Skills must be an object"))?;
    if map.len() > 256 {
        return Err(ApiError::new(413, "At most 256 saved skills"));
    }
    for (title, reference) in map {
        let normalized = request(&json!({"name":title,"rgd":reference}), false)?;
        if title.eq_ignore_ascii_case("remove")
            || normalized["name"] != *title
            || normalized["rgd"] != *reference
        {
            return Err(ApiError::new(400, "Invalid saved skill"));
        }
    }
    Ok(())
}
pub fn summary(text: &str) -> &str {
    for line in text.lines() {
        // `ist` labels each stored string; embedded subsequent lines are bare.
        let line = line
            .strip_prefix("string ")
            .and_then(|s| s.split_once(": "))
            .filter(|(n, _)| !n.is_empty() && n.bytes().all(|c| c.is_ascii_digit()))
            .map_or(line, |(_, text)| text);
        if line.starts_with("Summary") {
            return line;
        }
    }
    "<none>"
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn references_and_summaries_are_data() {
        assert_eq!(
            request(&json!({"name":"Screenshot","rgd":"I7S1"}), false).unwrap()["rgd"],
            "i7s1"
        );
        assert_eq!(
            request(&json!({"name":"REMOVE","rgd":"Screenshot"}), false).unwrap()["rgd"],
            "Screenshot"
        );
        for bad in [
            json!({"name":"x","rgd":"i7s1;bad"}),
            json!({"name":"x\ny","rgd":"i7s1"}),
            json!({"name":"x","rgd":"i7s1","extra":true}),
        ] {
            assert!(request(&bad, false).is_err());
        }
        assert_eq!(
            summary("string 1: Summary: capture\nSummary second"),
            "Summary: capture"
        );
        assert_eq!(
            summary("string 1: ignored\nSummary: multiline"),
            "Summary: multiline"
        );
        assert_eq!(
            summary("text Summary: not a prefix\n Summary: indented"),
            "<none>"
        );
    }
}
