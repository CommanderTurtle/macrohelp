//! Public overlay-control contract. No desktop input or task execution here.
use crate::client::{ApiError, Result};
use serde_json::{Value, json};

pub const LIMIT: usize = 1024 * 1024;
pub const INPUT_LIMIT: usize = 256;
pub const MENUS: &[&str] = &[
    "save_cursor",
    "paste_buffers",
    "registry_hub",
    "circle_placer",
    "click_left",
    "click_right",
    "click_middle",
    "stop_all",
    "view_toggles",
    "record_keys",
];
fn invalid(message: &str) -> ApiError {
    ApiError::new(400, message)
}

// CSV is a convenience syntax only. JSON arrays are unambiguous for arbitrary
// workflow text. Quoted trailing "no-replace" is data, never a hidden flag.
pub fn csv(text: &str) -> Result<Vec<(String, bool)>> {
    let mut out = Vec::new();
    let mut chars = text.chars().peekable();
    loop {
        let quoted = chars.peek() == Some(&'"');
        let mut value = String::new();
        if quoted {
            chars.next();
            loop {
                match chars.next() {
                    Some('"') if chars.peek() == Some(&'"') => {
                        chars.next();
                        value.push('"');
                    }
                    Some('"') => break,
                    Some(c) => value.push(c),
                    None => return Err(invalid("Unclosed CSV quote")),
                }
            }
            while chars.peek().is_some_and(|c| *c == ' ' || *c == '\t') {
                chars.next();
            }
            if chars.peek().is_some_and(|c| *c != ',') {
                return Err(invalid("Expected a comma after the quoted field"));
            }
        } else {
            while chars.peek().is_some_and(|c| *c != ',') {
                let c = chars.next().unwrap();
                if c == '"' {
                    return Err(invalid("Quote the entire CSV field"));
                }
                value.push(c);
            }
            value = value.trim().into();
        }
        out.push((value, quoted));
        if out.len() > INPUT_LIMIT + 2 {
            return Err(invalid("Too many inputs"));
        }
        if chars.next().is_none() {
            break;
        }
    }
    Ok(out)
}

pub fn normalize(value: &Value) -> Result<Value> {
    fn safe(value: &Value, depth: usize) -> Result<()> {
        if depth >= 64 {
            return Err(invalid("JSON nesting exceeds 64 levels"));
        }
        match value {
            Value::String(s) if s.contains('\0') => {
                return Err(invalid("NUL text is not supported"));
            }
            Value::Array(a) => {
                for v in a {
                    safe(v, depth + 1)?;
                }
            }
            Value::Object(o) => {
                for v in o.values() {
                    safe(v, depth + 1)?;
                }
            }
            _ => (),
        }
        Ok(())
    }
    safe(value, 0)?;
    let mut request = match value {
        Value::String(text) => {
            let mut fields = csv(text)?;
            let no_replace = fields.last().is_some_and(|(v, q)| !q && v == "no-replace");
            if no_replace {
                fields.pop();
            }
            if fields.is_empty() {
                return Err(invalid("Choose a menu"));
            }
            let menu = fields.remove(0).0;
            json!({"menu":menu,"inputs":fields.into_iter().map(|(v,_)|v).collect::<Vec<_>>(),"no_replace":no_replace})
        }
        Value::Object(_) => value.clone(),
        _ => return Err(invalid("Expected a control object or a CSV JSON string")),
    };
    let object = request.as_object().unwrap();
    if object
        .keys()
        .any(|k| !["menu", "inputs", "config", "no_replace", "origin", "run"].contains(&k.as_str()))
    {
        return Err(invalid("Unknown control field"));
    }
    if request
        .get("run")
        .is_some_and(|v| v.as_str().is_none_or(|s| s.is_empty() || s.len() > 80))
    {
        return Err(invalid("Invalid control run ID"));
    }
    let menu = if let Some(n) = request["menu"].as_u64() {
        Some(n as usize).filter(|_| n < 10)
    } else {
        request["menu"].as_str().and_then(|name| {
            MENUS.iter().position(|v| *v == name).or_else(|| {
                name.parse::<usize>()
                    .ok()
                    .filter(|n| *n < 10 && n.to_string() == name)
            })
        })
    };
    if let Some(menu) = menu {
        request["menu"] = json!(menu);
    } else if request.get("menu").is_some() {
        return Err(invalid("Menu must be 0–9 or a canonical menu name"));
    }
    if request.get("inputs").is_none() {
        request["inputs"] = json!([]);
    }
    let inputs = request["inputs"]
        .as_array()
        .filter(|a| a.len() <= INPUT_LIMIT)
        .ok_or_else(|| invalid("inputs must be an array of at most 256 entries"))?;
    if request.get("menu").is_none() && request.get("run").is_none() && !inputs.is_empty() {
        return Err(invalid("Choose a menu when supplying inputs"));
    }
    for input in inputs {
        if input.is_string() {
            continue;
        }
        let Some(action) = input.as_object() else {
            return Err(invalid("Each input must be a string or an action object"));
        };
        if action
            .keys()
            .any(|k| !["action", "value", "keys", "delay"].contains(&k.as_str()))
        {
            return Err(invalid("Unknown input action field"));
        }
        let name = input["action"]
            .as_str()
            .ok_or_else(|| invalid("Missing input action"))?;
        if ![
            "submit",
            "accept",
            "cancel",
            "play",
            "clear",
            "export",
            "import",
            "save_schedule",
            "paste",
            "paste_enter",
            "add_point",
            "save_click",
            "add_step",
            "capture",
            "manual",
            "add_paste",
            "replay",
        ]
        .contains(&name)
        {
            return Err(invalid("Unknown input action"));
        }
        if input.get("value").is_some_and(|v| !v.is_string()) {
            return Err(invalid("Action value must be text"));
        }
        if input.get("keys").is_some_and(|v| {
            v.as_array().is_none_or(|a| {
                a.is_empty()
                    || a.len() > 16
                    || a.iter()
                        .any(|k| k.as_str().is_none_or(|s| s.is_empty() || s.len() > 128))
            })
        }) {
            return Err(invalid("Choose 1–16 key names"));
        }
        if input
            .get("delay")
            .is_some_and(|v| v.as_u64().is_none_or(|n| !(10..=600000).contains(&n)))
        {
            return Err(invalid("Step delay must be 10–600000 ms"));
        }
    }
    if request.get("no_replace").is_some_and(|v| !v.is_boolean()) {
        return Err(invalid("no_replace must be boolean"));
    }
    if request.get("config").is_none() {
        request["config"] = json!({});
    }
    validate_patch(&request["config"])?;
    if let Some(origin) = request.get("origin") {
        coordinates(origin, 2)?;
    }
    if request.to_string().len() > LIMIT {
        return Err(ApiError::new(413, "Control request exceeds 1 MiB"));
    }
    if request.get("run").is_none() || request.get("no_replace").is_some() {
        request["no_replace"] = json!(request["no_replace"] == true);
    }
    Ok(request)
}

fn coordinates(v: &Value, count: usize) -> Result<()> {
    if v.as_array().is_none_or(|a| {
        a.len() != count
            || a.iter()
                .any(|n| n.as_i64().is_none_or(|n| !(-100000..=100000).contains(&n)))
    }) {
        return Err(invalid("Invalid integer coordinates"));
    }
    Ok(())
}
pub fn validate_patch(value: &Value) -> Result<()> {
    let object = value
        .as_object()
        .ok_or_else(|| invalid("config must be an object"))?;
    for (section, values) in object {
        match section.as_str() {
            "script" | "registry" => {
                if !values.is_string() {
                    return Err(invalid("script and registry must be text"));
                }
            }
            "points" => {
                let points = values
                    .as_object()
                    .ok_or_else(|| invalid("points uses numbered keys 1–16"))?;
                for (key, value) in points {
                    if key
                        .parse::<usize>()
                        .ok()
                        .is_none_or(|n| !(1..=16).contains(&n) || n.to_string() != *key)
                    {
                        return Err(invalid("Point number must be 1–16"));
                    }
                    if !value.is_null() {
                        coordinates(value, 2)?;
                    }
                }
            }
            "vars" | "buffers" | "zones" | "shells" | "paths" | "view" => {
                let fields = values
                    .as_object()
                    .ok_or_else(|| invalid("Config section must be an object"))?;
                let allowed: &[&str] = match section.as_str() {
                    "vars" => &["A", "S", "D", "F"],
                    "buffers" | "zones" => &["Z", "X", "C", "V"],
                    "shells" => &["X", "C", "G", "P", "T"],
                    "paths" => &["import", "export"],
                    _ => &[
                        "crosshair",
                        "grid",
                        "coordinates",
                        "panel",
                        "keys",
                        "coordinates_only",
                    ],
                };
                for (key, value) in fields {
                    if !allowed.contains(&key.as_str()) {
                        return Err(invalid("Unknown config key"));
                    }
                    if section == "zones" {
                        if !value.is_null() {
                            coordinates(value, 4)?;
                        }
                    } else if section == "view" {
                        if !value.is_boolean() {
                            return Err(invalid("View values must be boolean"));
                        }
                    } else if !value.is_string() {
                        return Err(invalid("Config value must be text"));
                    }
                }
            }
            _ => return Err(invalid("Unknown config section")),
        }
    }
    Ok(())
}
pub fn schema() -> Value {
    let mut schema = json!({"version":1,"service":"macrohelp-overlay","menus":MENUS,"routes":["GET /v1/health","GET /v1/schema","GET /v1/state","POST /v1/control","POST /v1/stop"],"input_semantics":"Each string submits one dialog input, including its Enter. Action objects address buttons. CSV starts with a menu; quote fields containing commas. Partial menus return a run ID; include run to continue that menu.","no_replace":"Temporary config overrides for this run; otherwise save supplied fields. Omitted fields are retained. A continued run inherits no_replace.","request":{"type":"object","properties":{"menu":{"oneOf":[{"type":"integer","minimum":0,"maximum":9},{"type":"string","enum":MENUS}]},"inputs":{"type":"array","maxItems":INPUT_LIMIT,"items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"action":{"type":"string","enum":["submit","accept","cancel","play","clear","export","import","save_schedule","paste","paste_enter","add_point","save_click","add_step","capture","manual","add_paste","replay"]},"value":{"type":"string"},"keys":{"type":"array","items":{"type":"string"},"minItems":1,"maxItems":16},"delay":{"type":"integer","minimum":10,"maximum":600000}},"required":["action"],"additionalProperties":false}]}},"config":{"type":"object"},"no_replace":{"type":"boolean","description":"Defaults false for new runs; omitted continuation values inherit the active run."},"origin":{"type":"array","items":{"type":"integer","minimum":-100000,"maximum":100000},"minItems":2,"maxItems":2},"run":{"type":"string","description":"The active run ID returned by a partial menu operation."}},"additionalProperties":false}});
    schema["routes"]
        .as_array_mut()
        .unwrap()
        .insert(3, json!("GET /v1/coords"));
    let mut sections = json!({"script":{"type":"string"},"registry":{"type":"string"}});
    for (section, keys) in [
        ("vars", vec!["A", "S", "D", "F"]),
        ("buffers", vec!["Z", "X", "C", "V"]),
        ("zones", vec!["Z", "X", "C", "V"]),
        ("shells", vec!["X", "C", "G", "P", "T"]),
        ("paths", vec!["import", "export"]),
        (
            "view",
            vec![
                "crosshair",
                "grid",
                "coordinates",
                "panel",
                "keys",
                "coordinates_only",
            ],
        ),
    ] {
        let mut properties = json!({});
        for key in keys {
            properties[key] = if section == "zones" {
                coordinate_schema(4)
            } else if section == "view" {
                json!({"type":"boolean"})
            } else {
                json!({"type":"string"})
            };
        }
        sections[section] =
            json!({"type":"object","properties":properties,"additionalProperties":false});
    }
    let mut points = json!({});
    for n in 1..=16 {
        points[n.to_string()] = coordinate_schema(2);
    }
    sections["points"] = json!({"type":"object","properties":points,"additionalProperties":false});
    schema["request"]["properties"]["config"] =
        json!({"type":"object","properties":sections,"additionalProperties":false});
    schema
}
fn coordinate_schema(count: usize) -> Value {
    json!({"oneOf":[{"type":"null"},{"type":"array","minItems":count,"maxItems":count,"items":{"type":"integer","minimum":-100000,"maximum":100000}}]})
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn comma_inputs_are_quoted_not_split_blindly() {
        assert_eq!(
            normalize(&json!("2,Z,\"{type hello, world}\",P,no-replace")).unwrap(),
            json!({"menu":2,"inputs":["Z","{type hello, world}","P"],"config":{},"no_replace":true})
        );
        assert_eq!(
            normalize(&json!("1,Z,\"no-replace\"")).unwrap()["inputs"],
            json!(["Z", "no-replace"])
        );
        assert!(normalize(&json!("2,\"unclosed")).is_err());
        assert_eq!(csv("1,\"a\"\"b\n,c\", ").unwrap()[1].0, "a\"b\n,c");
    }
    #[test]
    fn strict_types_prevent_accidental_config_loss() {
        for value in [
            json!({"menu":10}),
            json!({"inputs":["Z"]}),
            json!({"no_replace":"false"}),
            json!({"config":{"points":{"0":[1,2]}}}),
            json!({"config":{"view":{"keys":"false"}}}),
            json!({"config":{"zones":{"Z":[1,2,3]}}}),
            json!({"inputs":[{"action":"exec"}],"menu":2}),
        ] {
            assert!(normalize(&value).is_err(), "{value}");
        }
        assert!(normalize(&json!({"menu":"registry_hub","config":{"vars":{"A":"a,b"},"points":{"16":[-1,20]},"zones":{"C":null}},"no_replace":true})).is_ok());
    }
}
