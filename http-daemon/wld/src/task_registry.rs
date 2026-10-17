//! TaskRegistry counterpart: all task IDs/states come from the shared engine.
use crate::types::{now_ms, timestamp};
use serde_json::{Value, json};
pub fn task(job: &Value, verbose: bool) -> Value {
    let state = job["state"].as_str().unwrap_or("idle");
    let mut v = json!({"task_number":job["number"],"name":job["task"],"state":state,"delay_seconds":job["delay_seconds"],"loop_times":job["loop_times"],"created_at":timestamp(job["created_ms"].as_u64()),"scheduled_at":timestamp(job["due_ms"].as_u64())});
    v["session"] = job["session"].clone();
    if state == "scheduled" {
        v["remaining_seconds"] = json!(
            job["due_ms"]
                .as_u64()
                .unwrap_or_default()
                .saturating_sub(now_ms())
                / 1000
        );
    }
    if verbose {
        v["started_at"] = json!(timestamp(job["started_ms"].as_u64()));
        v["finished_at"] = json!(timestamp(job["finished_ms"].as_u64()));
        v["description"] = job["description"].clone();
        if job["error"].as_str().is_some_and(|s| !s.is_empty()) {
            v["error"] = job["error"].clone();
        }
        v["action"] = job["action"].clone();
        v["completed_loops"] = job["completed_loops"].clone();
    }
    v
}
pub fn status(jobs: &[Value]) -> Value {
    let mut scheduled = Vec::new();
    let mut running = Vec::new();
    let mut finished = Vec::new();
    for job in jobs {
        match job["state"].as_str().unwrap_or("") {
            "scheduled" => scheduled.push(task(job, false)),
            "running" => running.push(task(job, false)),
            _ => finished.push(task(job, false)),
        }
    }
    json!({"scheduled_count":scheduled.len(),"running_count":running.len(),"finished_count":finished.len(),"total_count":jobs.len(),"scheduled":scheduled,"running":running,"finished":finished})
}
pub fn message(job: &Value) -> String {
    let name = job["task"].as_str().unwrap_or("");
    match job["state"].as_str().unwrap_or("") {
        "scheduled" => format!(
            "'{name}' is scheduled to run in ~{}s",
            job["due_ms"]
                .as_u64()
                .unwrap_or_default()
                .saturating_sub(now_ms())
                / 1000
        ),
        "running" => format!(
            "'{name}' is currently running (elapsed {}s)",
            now_ms().saturating_sub(job["started_ms"].as_u64().unwrap_or_else(now_ms)) / 1000
        ),
        "finished" => format!("'{name}' has finished"),
        "stopped" => format!("'{name}' was stopped before completion"),
        "failed" => format!("'{name}' failed: {}", job["error"].as_str().unwrap_or("")),
        _ => format!("'{name}' is idle"),
    }
}
