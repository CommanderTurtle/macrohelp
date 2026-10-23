//! Shared wire fixtures. Optional native executable also checks C++ schema and
//! normalization byte-for-byte as JSON values, without starting either GUI.
use macrohelp_overlay::control_protocol;
use serde_json::{Value, json};
use std::{
    io::Write,
    process::{Command, Stdio},
};
#[test]
fn shared_wire_fixtures_and_optional_cpp_parity() {
    let fixtures: Value =
        serde_json::from_str(include_str!("../../tests/control-fixtures.json")).unwrap();
    let expected: Vec<_> = fixtures
        .as_array()
        .unwrap()
        .iter()
        .map(|test| {
            let result = control_protocol::normalize(&test["input"]);
            assert_eq!(
                result.is_ok(),
                test["valid"].as_bool().unwrap(),
                "{}",
                test["input"]
            );
            result
        })
        .collect();
    let Some(exe) = std::env::var_os("MACROHELP_CPP_CONTROL_TEST") else {
        return;
    };
    let schema = Command::new(&exe).arg("--schema").output().unwrap();
    assert!(schema.status.success());
    assert_eq!(
        serde_json::from_slice::<Value>(&schema.stdout).unwrap(),
        control_protocol::schema()
    );
    let mut child = Command::new(&exe)
        .arg("--normalize")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut input = child.stdin.take().unwrap();
    for test in fixtures.as_array().unwrap() {
        writeln!(input, "{}", test["input"]).unwrap();
    }
    drop(input);
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    let text = String::from_utf8(output.stdout).unwrap();
    let replies: Vec<Value> = text
        .lines()
        .map(|line| serde_json::from_str(line).unwrap())
        .collect();
    assert_eq!(replies.len(), expected.len());
    for (reply, result) in replies.iter().zip(expected) {
        match result {
            Ok(value) => assert_eq!(*reply, json!({"ok":true,"result":value})),
            Err(e) => assert_eq!(reply["error"]["status"], e.status),
        }
    }
}
