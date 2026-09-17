pub fn get_string(document: &serde_json::Value, key: &str) -> String {
    document
        .get(key)
        .and_then(|field_value| field_value.as_str())
        .unwrap_or("")
        .to_string()
}

pub fn get_string_or(document: &serde_json::Value, key: &str, default: &str) -> String {
    document
        .get(key)
        .and_then(|field_value| field_value.as_str())
        .unwrap_or(default)
        .to_string()
}

pub fn get_int(document: &serde_json::Value, key: &str) -> i64 {
    document
        .get(key)
        .and_then(|field_value| field_value.as_i64())
        .unwrap_or(0)
}

pub fn get_int_or(document: &serde_json::Value, key: &str, default: i64) -> i64 {
    document
        .get(key)
        .and_then(|field_value| field_value.as_i64())
        .unwrap_or(default)
}
