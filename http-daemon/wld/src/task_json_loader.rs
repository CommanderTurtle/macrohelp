//! TaskJsonLoader counterpart: normalize the existing Tasket document contract.
use crate::types::{ApiError, Result, name};
use serde_json::{Value, json};
pub fn load(raw: &Value, id: &str, delay: u64, loops: i64, allow_system: bool) -> Result<Value> {
    let id = name(id)?;
    if raw["docType"]
        .as_str()
        .is_some_and(|s| s != "ScheduleTask File")
    {
        return Err(ApiError::new(400, "Not a Tasket schedule document"));
    }
    let actions = raw["actions"]
        .as_array()
        .ok_or_else(|| ApiError::new(400, "Task must contain an actions array"))?;
    if actions.len() > 10000 {
        return Err(ApiError::new(400, "Task exceeds 10000 actions"));
    }
    let mut normalized = Vec::new();
    for action in actions {
        let mut a = action.clone();
        if a["type"] == "WaitAction" {
            let ms = a["timeToWaitInMs"]
                .as_f64()
                .ok_or_else(|| ApiError::new(400, "Invalid legacy wait"))?;
            a = json!({"type":"wait","duration":ms/1000.0});
        }
        let kind = a["type"]
            .as_str()
            .ok_or_else(|| ApiError::new(400, "Missing action type"))?;
        if ![
            "paste",
            "wait",
            "keyssequence",
            "systemcommand",
            "cursormovements",
            "runningothertask",
            "move",
            "click",
            "scroll",
            "hotkey",
            "type",
        ]
        .contains(&kind)
        {
            return Err(ApiError::new(400, format!("Unknown action type: {kind}")));
        }
        if kind == "systemcommand" && !allow_system {
            return Err(ApiError::new(
                403,
                "HTTP system commands require MACROHELP_ALLOW_SYSTEM=true",
            ));
        }
        normalized.push(a);
    }
    Ok(
        json!({"id":id,"description":raw["description"].as_str().unwrap_or(""),"delay_seconds":delay,"loop_times":loops,"auto_start":false,"allow_system":allow_system,"actions":normalized}),
    )
}
