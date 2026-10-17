//! TaskRunner counterpart: prepare a plan, then use the one Omarsket scheduler.
use crate::{
    config::Config,
    task_executor::{Executor, snapshot},
    task_json_loader, task_registry,
    types::{ApiError, Result, integer, name, string},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeSet,
    sync::{Arc, Mutex},
};
pub struct Runner {
    pub executor: Arc<dyn Executor>,
    pub config: Config,
    owned: Mutex<BTreeSet<(String, u64)>>,
}
impl Runner {
    pub fn new(config: Config, executor: Arc<dyn Executor>) -> Self {
        Self {
            config,
            executor,
            owned: Mutex::new(BTreeSet::new()),
        }
    }
    pub fn snapshot(&self) -> Result<Value> {
        snapshot(self.executor.as_ref())
    }
    pub fn find<'a>(&self, s: &'a Value, raw: &str) -> Result<&'a Value> {
        let id = name(raw)?;
        let tasks = s["tasks"]
            .as_array()
            .ok_or_else(|| ApiError::new(502, "Missing Omarsket tasks"))?;
        tasks
            .iter()
            .find(|t| t["id"] == id)
            .or_else(|| {
                tasks.iter().find(|t| {
                    t["id"]
                        .as_str()
                        .is_some_and(|n| n.eq_ignore_ascii_case(&id))
                })
            })
            .ok_or_else(|| ApiError::new(404, format!("Task '{id}' not found")))
    }
    fn track(&self, job: Value) -> Result<Value> {
        let id = job["number"]
            .as_u64()
            .ok_or_else(|| ApiError::new(502, "Missing task number"))?;
        let mut owned = self.owned.lock().unwrap();
        if owned.len() >= 2048 {
            let s = self.snapshot()?;
            owned.retain(|(session, id)| {
                s["jobs"].as_array().is_some_and(|jobs| {
                    jobs.iter().any(|j| {
                        j["number"] == *id
                            && j["session"] == *session
                            && ["scheduled", "running"].contains(&j["state"].as_str().unwrap_or(""))
                    })
                })
            });
        }
        let session = job["session"].as_str().ok_or_else(|| {
            ApiError::new(502, "Update Omarsket: missing engine session identity")
        })?;
        owned.insert((session.to_owned(), id));
        Ok(job)
    }
    pub fn run(&self, body: &Value) -> Result<Value> {
        self.executor.call(json!({"method":"reload"}))?;
        let s = self.snapshot()?;
        let task = self.find(&s, string(body, "task")?)?;
        let delay = integer(
            &body["delay"],
            task["delay_seconds"]
                .as_i64()
                .unwrap_or(self.config.default_delay as i64),
            0,
            86400,
        )?;
        let loops = integer(&body["loop"], 1, -1, 100000)?;
        if loops == 0 {
            return Err(ApiError::new(400, "Loop count cannot be zero"));
        }
        self.track(
            self.executor
                .call(json!({"method":"run","name":task["id"],"delay":delay,"loop":loops}))?,
        )
    }
    pub fn temporary(&self, body: &Value) -> Result<Value> {
        let id = name(string(body, "name")?)?;
        let delay = integer(
            &body["delay"],
            body["task"]["http_delay"]
                .as_i64()
                .or(body["task"]["delay"].as_i64())
                .unwrap_or(self.config.default_delay as i64),
            0,
            86400,
        )?;
        let loops = integer(&body["loop"], 1, -1, 100000)?;
        if loops == 0 {
            return Err(ApiError::new(400, "Loop count cannot be zero"));
        }
        let task = task_json_loader::load(
            &body["task"],
            &id,
            delay as u64,
            loops,
            self.config.allow_system,
        )?;
        self.executor
            .call(json!({"method":"validate-task","task":task}))?;
        for key in ["run", "cleanup"] {
            if !body[key].is_null() && !body[key].is_boolean() {
                return Err(ApiError::new(400, format!("{key} must be boolean")));
            }
        }
        let run = body["run"].as_bool().unwrap_or(true);
        let cleanup = body["cleanup"].as_bool().unwrap_or(false);
        let s = self.snapshot()?;
        if s["tasks"]
            .as_array()
            .is_some_and(|a| a.iter().any(|t| t["id"] == id))
        {
            return Err(ApiError::new(
                409,
                "Temporary task name collides with a saved schedule",
            ));
        }
        if !cleanup {
            self.executor
                .call(json!({"method":"save","task":task,"create":true}))?;
        }
        let job = if run {
            Some(self.track(self.executor.call(if cleanup {
                json!({"method":"run-temporary","task":task})
            } else {
                json!({"method":"run","name":id,"delay":delay,"loop":loops})
            })?)?)
        } else {
            None
        };
        Ok(
            json!({"name":id,"path":if cleanup {String::new()}else{format!("{}/schedules/{id}.omscht",s["directory"].as_str().unwrap_or(""))},"created":true,"scheduled":run,"cleanup":cleanup,"storage":if cleanup{"memory"}else{"file"},"task":job.as_ref().map(|j|task_registry::task(j,false))}),
        )
    }
    pub fn stop(&self, id: Option<u64>) -> Result<()> {
        if let Some(id) = id {
            let s = self.snapshot()?;
            if !s["jobs"]
                .as_array()
                .is_some_and(|a| a.iter().any(|j| j["number"] == id))
            {
                return Err(ApiError::new(404, format!("Task #{id} not found")));
            }
        }
        self.executor.call(json!({"method":"stop","number":id}))?;
        Ok(())
    }
    pub fn stop_owned(&self) {
        for (session, id) in self.owned.lock().unwrap().iter() {
            let _ = self
                .executor
                .call(json!({"method":"stop","number":id,"session":session}));
        }
    }
}
