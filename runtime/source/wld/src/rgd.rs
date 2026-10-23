//! Registry includes use the scheduler's Run Command, not a local subprocess.
use crate::client::{ApiError, Result};
use serde_json::{Value, json};
use std::{
    fs,
    io::Read,
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

static GENERATION: AtomicU64 = AtomicU64::new(0);
static SERIAL: AtomicU64 = AtomicU64::new(0);
const LIMIT: u64 = 1024 * 1024;
pub fn cancel() {
    GENERATION.fetch_add(1, Ordering::SeqCst);
}
pub fn generation() -> u64 {
    GENERATION.load(Ordering::SeqCst)
}

fn io(error: std::io::Error) -> ApiError {
    ApiError::new(500, error.to_string())
}
fn quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}
pub(crate) fn valid(reference: &str) -> bool {
    reference
        .strip_prefix('i')
        .and_then(|s| s.split_once(['s', 'z']))
        .is_some_and(|(a, b)| {
            !a.is_empty() && !b.is_empty() && a.bytes().chain(b.bytes()).all(|c| c.is_ascii_digit())
        })
}
struct Output(PathBuf);
impl Output {
    fn new(schedules: &Path) -> Result<Self> {
        if !schedules.is_absolute() || !schedules.is_dir() {
            return Err(ApiError::new(
                503,
                "The controller schedule directory is unavailable",
            ));
        }
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let path = schedules.join(format!(
            ".macrohelp-rgd-{}-{nonce}-{}",
            std::process::id(),
            SERIAL.fetch_add(1, Ordering::Relaxed)
        ));
        let builder = fs::DirBuilder::new();
        #[cfg(unix)]
        let builder = {
            use std::os::unix::fs::DirBuilderExt;
            let mut builder = builder;
            builder.mode(0o700);
            builder
        };
        builder.create(&path).map_err(io)?;
        Ok(Self(path))
    }
    fn read(&self, name: &str) -> Result<String> {
        let path = self.0.join(name);
        let meta = fs::symlink_metadata(&path).map_err(io)?;
        if !meta.is_file() || meta.len() > LIMIT {
            return Err(ApiError::new(
                413,
                "rgd output exceeds 1 MiB or is not a regular file",
            ));
        }
        let mut text = String::new();
        fs::File::open(path)
            .map_err(io)?
            .take(LIMIT + 1)
            .read_to_string(&mut text)
            .map_err(io)?;
        if text.len() as u64 > LIMIT {
            return Err(ApiError::new(413, "rgd output exceeds 1 MiB"));
        }
        Ok(text)
    }
}
impl Drop for Output {
    fn drop(&mut self) {
        for name in ["output.tmp", "error.tmp", "status.tmp", "done"] {
            let _ = fs::remove_file(self.0.join(name));
        }
        let _ = fs::remove_dir(&self.0);
    }
}
fn task(output: &Output, file: &str, reference: &str, strings: bool) -> Value {
    // Positional parameters keep paths/data out of shell syntax. Publish status
    // last: Run Command itself returns before the detached writer has finished.
    let script = "umask 077\nrgd rg \"$1\" \"$2\" >\"$3/output.tmp\" 2>\"$3/error.tmp\"\ncode=$?\nprintf '%s' \"$code\" >\"$3/status.tmp\" && mv -f -- \"$3/status.tmp\" \"$3/done\"";
    let script = if strings { script.replace("rgd rg", "rgd ist") } else { script.into() };
    let index = reference[1..].split(['s','z']).next().unwrap();
    let reference = if strings { index } else { reference };
    let dir = output.0.to_string_lossy();
    let args = ["-c", &script, "macrohelp-rgd", file, reference, &dir]
        .map(quote)
        .join(" ");
    json!({"name":output.0.file_name().unwrap().to_string_lossy(),"run":true,"cleanup":true,"delay":0,"loop":1,
        "task":{"actions":[{"type":"systemcommand","sysCommandType":"executeprogram","sysCommandParam1":"sh","sysCommandParam2":args}]}})
}
pub fn read(
    file: &str,
    reference: &str,
    generation: u64,
    mut http: impl FnMut(&str, &str, &Value) -> Result<Value>,
) -> Result<Value> {
    read_with(
        file,
        reference,
        false,
        &mut http,
        || GENERATION.load(Ordering::SeqCst) != generation,
        Duration::from_secs(15),
    )
}
pub fn strings(file: &str, reference: &str, generation: u64, mut http: impl FnMut(&str,&str,&Value)->Result<Value>) -> Result<Value> {
    let value=read_with(file,reference,true,&mut http,||GENERATION.load(Ordering::SeqCst)!=generation,Duration::from_secs(15))?;
    Ok(json!({"summary":crate::control_skills::summary(value["text"].as_str().unwrap_or(""))}))
}
fn read_with(
    file: &str,
    reference: &str,
    strings: bool,
    http: &mut impl FnMut(&str, &str, &Value) -> Result<Value>,
    cancelled: impl Fn() -> bool,
    timeout: Duration,
) -> Result<Value> {
    if file.trim().is_empty() || file.starts_with('-') || file.contains('\0') || !valid(reference) {
        return Err(ApiError::new(
            400,
            "Use a Registry Hub file and {rgd i6s6} or {rgd i6z6}",
        ));
    }
    if cancelled() {
        return Err(ApiError::new(409, "rgd read cancelled"));
    }
    let health = http("GET", "/health", &json!({}))?;
    let schedules = health["tasks_dir"]
        .as_str()
        .ok_or_else(|| ApiError::new(502, "Controller did not return its schedule directory"))?;
    let output = Output::new(Path::new(schedules))?;
    if cancelled() {
        return Err(ApiError::new(409, "rgd read cancelled"));
    }
    http("POST", "/temp-task", &task(&output, file, reference, strings))?;
    let deadline = Instant::now() + timeout;
    loop {
        if cancelled() {
            return Err(ApiError::new(409, "rgd read cancelled"));
        }
        if output.0.join("done").exists() {
            if output.read("done")?.trim() != "0" {
                let error = output.read("error.tmp").unwrap_or_default();
                return Err(ApiError::new(400, format!("rgd failed: {}", error.trim())));
            }
            return Ok(json!({"text":output.read("output.tmp")?}));
        }
        if Instant::now() >= deadline {
            return Err(ApiError::new(
                504,
                "rgd Run Command did not finish within 15 seconds",
            ));
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn summary_reads_use_ist_through_the_same_run_command() {
        let base=std::env::temp_dir().join(format!("macrohelp-ist-test-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        fs::create_dir(&base).unwrap();
        let result=read_with("/data/a 'quoted' registry","i7s1",true,&mut |_,path,body| {
            if path=="/health" {return Ok(json!({"tasks_dir":base}));}
            let action=&body["task"]["actions"][0];let args=action["sysCommandParam2"].as_str().unwrap();
            assert_eq!(action["sysCommandType"],"executeprogram");assert!(args.contains("rgd ist"));
            assert!(args.contains("'7'"));assert!(!args.contains("i7s1"));
            let staged=base.join(body["name"].as_str().unwrap());
            fs::write(staged.join("output.tmp"),"string 1: Summary: safe metadata").unwrap();
            fs::write(staged.join("done"),"0").unwrap();Ok(json!({}))
        },||false,Duration::ZERO).unwrap();
        assert_eq!(crate::control_skills::summary(result["text"].as_str().unwrap()),"Summary: safe metadata");
        assert_eq!(fs::read_dir(&base).unwrap().count(),0);fs::remove_dir(base).unwrap();
    }
    #[test]
    fn run_command_output_is_private_atomic_and_ephemeral() {
        let base = std::env::temp_dir().join(format!(
            "macrohelp-rgd-test-{}",
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&base).unwrap();
        let text = "{type {\n\"x\": \"literal\\n\"\n}}";
        for outcome in [
            "success", "failure", "timeout", "cancel", "denied", "oversize",
        ] {
            let mut staged = PathBuf::new();
            let cancelled = std::cell::Cell::new(false);
            let result = read_with(
                "/home/me/a 'quoted;$ file",
                "i6z6",
                false,
                &mut |method, path, body| {
                    if path == "/health" {
                        assert_eq!(method, "GET");
                        return Ok(json!({"tasks_dir":base}));
                    }
                    assert_eq!((method, path), ("POST", "/temp-task"));
                    let name = body["name"].as_str().unwrap();
                    staged = base.join(name);
                    assert!(staged.is_dir());
                    #[cfg(unix)]
                    {
                        use std::os::unix::fs::PermissionsExt;
                        assert_eq!(
                            fs::metadata(&staged).unwrap().permissions().mode() & 0o777,
                            0o700
                        );
                    }
                    let action = &body["task"]["actions"][0];
                    assert_eq!(action["sysCommandType"], "executeprogram");
                    assert_eq!(action["sysCommandParam1"], "sh");
                    assert!(
                        action["sysCommandParam2"]
                            .as_str()
                            .unwrap()
                            .contains("a '\\''quoted;$ file")
                    );
                    assert_eq!(body["cleanup"], true);
                    if outcome == "denied" {
                        return Err(ApiError::new(403, "Run Command permission required"));
                    }
                    fs::write(
                        staged.join("output.tmp"),
                        if outcome == "oversize" {
                            "x".repeat(LIMIT as usize + 1)
                        } else {
                            text.into()
                        },
                    )
                    .unwrap();
                    fs::write(staged.join("error.tmp"), "missing rgd").unwrap();
                    if outcome != "timeout" {
                        fs::write(
                            staged.join("done"),
                            if outcome == "failure" { "127" } else { "0" },
                        )
                        .unwrap();
                    }
                    cancelled.set(outcome == "cancel");
                    Ok(json!({}))
                },
                || cancelled.get(),
                Duration::ZERO,
            );
            match outcome {
                "success" => assert_eq!(result.unwrap()["text"], text),
                "failure" => assert!(result.unwrap_err().message.contains("missing rgd")),
                "timeout" => assert_eq!(result.unwrap_err().status, 504),
                "cancel" => assert_eq!(result.unwrap_err().status, 409),
                "denied" => assert_eq!(result.unwrap_err().status, 403),
                _ => assert_eq!(result.unwrap_err().status, 413),
            }
            assert!(!staged.exists(), "left {outcome} output behind");
        }
        fs::remove_dir(base).unwrap();
        for bad in ["i1", "i1s", "--help", "i1s2;echo", "i1s2z3"] {
            assert!(!valid(bad));
        }
    }
}
