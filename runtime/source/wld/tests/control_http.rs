use macrohelp_overlay::{
    client::ApiError,
    control_http::{Dispatch, Server, Settings},
};
use serde_json::{Value, json};
use std::{
    io::{Read, Write},
    net::TcpStream,
    sync::{Arc, Mutex},
};
const KEY: &str = "control-contract-test-key-0123456789";
fn call(server: &Server, method: &str, path: &str, body: &str, headers: &str) -> (u16, Value) {
    let mut stream = TcpStream::connect(server.address).unwrap();
    stream
        .set_read_timeout(Some(std::time::Duration::from_secs(3)))
        .unwrap();
    write!(
        stream,
        "{method} {path} HTTP/1.1\r\nHost: {}\r\nContent-Length: {}\r\n{headers}\r\n{body}",
        server.address,
        body.len()
    )
    .unwrap();
    let mut result = String::new();
    stream.read_to_string(&mut result).unwrap();
    let (head, body) = result.split_once("\r\n\r\n").unwrap();
    (
        head.split_whitespace().nth(1).unwrap().parse().unwrap(),
        serde_json::from_str(body).unwrap(),
    )
}
#[test]
fn authenticated_http_normalizes_inputs_before_the_ui_boundary() {
    let seen = Arc::new(Mutex::new(Vec::new()));
    let records = seen.clone();
    let dispatch: Dispatch = Arc::new(move |method, path, body| {
        records
            .lock()
            .unwrap()
            .push((method.to_owned(), path.to_owned(), body.clone()));
        Ok(json!({"accepted":true}))
    });
    let server = Server::bind(
        Settings {
            address: ([127, 0, 0, 1], 0).into(),
            key: KEY.into(),
        },
        dispatch,
    )
    .unwrap();
    let auth = format!("Authorization: Bearer {KEY}\r\nContent-Type: application/json\r\n");
    assert_eq!(call(&server, "GET", "/v1/health", "", "").0, 401);
    assert_eq!(call(&server, "GET", "/v1/health", "", &auth).0, 200);
    assert_eq!(
        call(&server, "GET", "/v1/schema", "", &auth).1["result"]["menus"]
            .as_array()
            .unwrap()
            .len(),
        10
    );
    let body = json!("2,Z,\"{type hello, world}\",P,no-replace").to_string();
    assert_eq!(call(&server, "POST", "/v1/control", &body, &auth).0, 200);
    let records = seen.lock().unwrap();
    let normalized = &records.last().unwrap().2;
    assert_eq!(
        normalized["inputs"],
        json!(["Z", "{type hello, world}", "P"])
    );
    assert_eq!(normalized["no_replace"], true);
    drop(records);
    assert_eq!(call(&server, "POST", "/v1/control", "{bad", &auth).0, 400);
    assert_eq!(
        call(&server, "POST", "/v1/control", r#"{"menu":99}"#, &auth).0,
        400
    );
    assert_eq!(
        call(
            &server,
            "POST",
            "/v1/control",
            "{}",
            &format!("Authorization: Bearer {KEY}\r\n")
        )
        .0,
        415
    );
    assert_eq!(call(&server, "GET", "/other", "", &auth).0, 404);
    assert_eq!(seen.lock().unwrap().len(), 2);
}
#[test]
fn busy_is_explicit_and_stop_uses_the_same_authenticated_route() {
    let dispatch: Dispatch = Arc::new(|_, path, _| {
        if path == "/v1/control" {
            Err(ApiError::new(409, "Overlay is busy"))
        } else {
            Ok(json!({"stop_requested":true}))
        }
    });
    let server = Server::bind(
        Settings {
            address: ([127, 0, 0, 1], 0).into(),
            key: KEY.into(),
        },
        dispatch,
    )
    .unwrap();
    let auth = format!("Authorization: Bearer {KEY}\r\nContent-Type: application/json\r\n");
    let (status, value) = call(&server, "POST", "/v1/control", "{}", &auth);
    assert_eq!(status, 409);
    assert_eq!(value["error"]["message"], "Overlay is busy");
    assert_eq!(
        call(&server, "POST", "/v1/stop", "{}", &auth).1["result"]["stop_requested"],
        true
    );
}
#[test]
fn malformed_framing_does_not_reach_the_overlay() {
    let dispatch: Dispatch = Arc::new(|_, _, _| panic!("Invalid framing reached overlay"));
    let server = Server::bind(
        Settings {
            address: ([127, 0, 0, 1], 0).into(),
            key: KEY.into(),
        },
        dispatch,
    )
    .unwrap();
    let mut stream = TcpStream::connect(server.address).unwrap();
    write!(stream,"POST /v1/control HTTP/1.1\r\nHost: {}\r\nAuthorization: Bearer {KEY}\r\nContent-Type: application/json\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\n{{}}",server.address).unwrap();
    let mut result = String::new();
    stream.read_to_string(&mut result).unwrap();
    assert!(result.starts_with("HTTP/1.1 400"));
}
