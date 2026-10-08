use rand::RngCore;
use serde_json::{json, Value};
use std::{
    fs::{self, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
};
use zeroize::Zeroizing;

const CONTROL_TOKEN_KEY: &str = "PIXELS_CONSOLE_BACKUP_CONTROL_TOKEN";
const MAXIMUM_CONFIG_BYTES: u64 = 16 * 1024;

pub fn upgrade_single_server_backup_control(
    config_root: &Path,
    platform: &str,
) -> Result<bool, String> {
    let console_host = match platform {
        "windows" => "127.0.0.1",
        "linux" => "127.0.0.1",
        _ => return Err("invalid Single Server platform".into()),
    };
    if !config_root.is_absolute() {
        return Err("configuration root must be absolute".into());
    }
    let console_path = config_root.join("console.env");
    let relay_path = config_root.join("relay.env");
    let backup_path = config_root.join("backup.json");
    let existing_files = [&console_path, &relay_path, &backup_path]
        .iter()
        .filter(|path| path.exists())
        .count();
    if existing_files == 0 {
        return Ok(false);
    }
    if existing_files != 3 || !config_root.join("setup.complete").is_file() {
        return Err("incomplete Single Server configuration".into());
    }
    let console_bytes = read_config(&console_path)?;
    let relay_bytes = read_config(&relay_path)?;
    let backup_bytes = read_config(&backup_path)?;
    let console_environment =
        std::str::from_utf8(&console_bytes).map_err(|_| "invalid Console environment")?;
    let relay_environment =
        std::str::from_utf8(&relay_bytes).map_err(|_| "invalid Relay environment")?;
    if environment_value(console_environment, "PIXELS_CONSOLE_DISTRIBUTION")? != Some("official")
        || environment_value(console_environment, "PIXELS_CONSOLE_RELEASE_NAMESPACE")?
            != Some("pixels.official")
    {
        return Err("Single Server requires the Official release identity".into());
    }
    let console_deployment = environment_value(console_environment, "PIXELS_DEPLOYMENT_ID")?
        .ok_or("Console deployment identity is missing")?;
    let relay_deployment = environment_value(relay_environment, "PIXELS_DEPLOYMENT_ID")?
        .ok_or("Relay deployment identity is missing")?;
    let mut backup_configuration: Value =
        serde_json::from_slice(&backup_bytes).map_err(|_| "invalid Backup configuration")?;
    let backup_deployment = backup_configuration
        .get("deployment_id")
        .and_then(Value::as_str)
        .ok_or("Backup deployment identity is missing")?;
    if console_deployment != relay_deployment || console_deployment != backup_deployment {
        return Err("Single Server deployment identities differ".into());
    }
    let console_ca_file = environment_value(relay_environment, "PIXELS_RELAY_CONSOLE_CA_FILE")?
        .ok_or("Relay Console CA is missing")?;
    let console_ca_path = PathBuf::from(console_ca_file);
    if !console_ca_path.is_absolute() || !console_ca_path.is_file() {
        return Err("Relay Console CA is unavailable".into());
    }
    let console_token = environment_value(console_environment, CONTROL_TOKEN_KEY)?;
    let backup_control = backup_configuration.get("control");
    let backup_token = backup_control
        .and_then(|control| control.get("token"))
        .and_then(Value::as_str);
    for existing_token in [console_token, backup_token].into_iter().flatten() {
        if !valid_token(existing_token) {
            return Err("Backup control token is invalid".into());
        }
    }
    if let (Some(console_token), Some(backup_token)) = (console_token, backup_token) {
        if console_token != backup_token {
            return Err("Backup control tokens differ".into());
        }
    }
    if let Some(control) = backup_control {
        let expected_url = format!("wss://{console_host}:4600/api/console/backup-control");
        if control.get("console_url").and_then(Value::as_str) != Some(expected_url.as_str())
            || control.get("console_ca_file").and_then(Value::as_str) != Some(console_ca_file)
            || backup_token.is_none()
        {
            return Err("Backup control endpoint or CA differs".into());
        }
    }
    if console_token.is_some() && backup_control.is_some() {
        return Ok(false);
    }
    let generated_token = if console_token.is_none() && backup_token.is_none() {
        let mut random_bytes = [0u8; 32];
        rand::rng().fill_bytes(&mut random_bytes);
        Some(Zeroizing::new(hex::encode(random_bytes)))
    } else {
        None
    };
    let control_token = console_token
        .or(backup_token)
        .or_else(|| generated_token.as_deref().map(String::as_str))
        .ok_or("Backup control token is unavailable")?;
    let mut next_console = Zeroizing::new(console_bytes.to_vec());
    if console_token.is_none() {
        if !next_console.ends_with(b"\n") {
            next_console.push(b'\n');
        }
        next_console.extend_from_slice(format!("{CONTROL_TOKEN_KEY}={control_token}\n").as_bytes());
    }
    let next_backup = if backup_control.is_none() {
        backup_configuration["control"] = json!({
            "console_url": format!("wss://{console_host}:4600/api/console/backup-control"),
            "console_ca_file": console_ca_file,
            "token": control_token,
        });
        Some(Zeroizing::new(
            serde_json::to_vec_pretty(&backup_configuration)
                .map_err(|_| "Backup configuration serialization failed")?,
        ))
    } else {
        None
    };
    let update_result = (|| {
        if console_token.is_none() {
            write_config(&console_path, &next_console)?;
        }
        if let Some(next_backup) = &next_backup {
            write_config(&backup_path, next_backup)?;
        }
        Ok::<(), String>(())
    })();
    if let Err(update_error) = update_result {
        let console_restore = write_config(&console_path, &console_bytes);
        let backup_restore = write_config(&backup_path, &backup_bytes);
        if console_restore.is_err() || backup_restore.is_err() {
            return Err("Backup control upgrade failed and configuration rollback failed".into());
        }
        return Err(update_error);
    }
    Ok(true)
}

fn read_config(path: &Path) -> Result<Zeroizing<Vec<u8>>, String> {
    let metadata = fs::symlink_metadata(path).map_err(|_| "configuration file is unavailable")?;
    if !metadata.is_file()
        || metadata.file_type().is_symlink()
        || metadata.len() > MAXIMUM_CONFIG_BYTES
    {
        return Err("configuration file has an unsafe type or size".into());
    }
    fs::read(path)
        .map(Zeroizing::new)
        .map_err(|_| "configuration file cannot be read".into())
}

fn write_config(path: &Path, bytes: &[u8]) -> Result<(), String> {
    if bytes.is_empty() || bytes.len() as u64 > MAXIMUM_CONFIG_BYTES {
        return Err("updated configuration size is invalid".into());
    }
    let mut configuration_file = OpenOptions::new()
        .write(true)
        .truncate(true)
        .open(path)
        .map_err(|_| "configuration file cannot be opened for update")?;
    configuration_file
        .write_all(bytes)
        .map_err(|_| "configuration update failed")?;
    configuration_file
        .sync_all()
        .map_err(|_| "configuration update sync failed".into())
}

fn environment_value<'a>(environment: &'a str, key: &str) -> Result<Option<&'a str>, String> {
    let prefix = format!("{key}=");
    let mut values = environment
        .lines()
        .filter_map(|line| line.strip_prefix(&prefix))
        .map(str::trim_end);
    let value = values.next();
    if values.next().is_some() || value.is_some_and(str::is_empty) {
        return Err("environment key is duplicated or empty".into());
    }
    Ok(value)
}

fn valid_token(token: &str) -> bool {
    token.len() == 64
        && token
            .bytes()
            .all(|character| character.is_ascii_digit() || (b'a'..=b'f').contains(&character))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn creates_once_preserves_token_and_rejects_mismatch() {
        let test_directory = tempfile::tempdir().unwrap();
        let config_root = test_directory.path();
        assert!(!upgrade_single_server_backup_control(config_root, "windows").unwrap());
        let deployment_id = "c4fdfa68-d9d4-4e15-9883-344bd84c415c";
        fs::write(config_root.join("setup.complete"), b"ready\n").unwrap();
        fs::write(config_root.join("console-ca.crt"), b"test-ca").unwrap();
        fs::write(
            config_root.join("console.env"),
            format!("PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_CONSOLE_DISTRIBUTION=official\nPIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.official\n"),
        )
        .unwrap();
        fs::write(
            config_root.join("relay.env"),
            format!(
                "PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_RELAY_CONSOLE_CA_FILE={}\n",
                config_root.join("console-ca.crt").display()
            ),
        )
        .unwrap();
        fs::write(
            config_root.join("backup.json"),
            format!("{{\"deployment_id\":\"{deployment_id}\"}}"),
        )
        .unwrap();
        fs::write(
            config_root.join("console.env"),
            format!("PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_CONSOLE_DISTRIBUTION=customer\nPIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer\n"),
        )
        .unwrap();
        assert!(upgrade_single_server_backup_control(config_root, "windows").is_err());
        fs::write(
            config_root.join("console.env"),
            format!("PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_CONSOLE_DISTRIBUTION=official\nPIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.official\n"),
        )
        .unwrap();
        assert!(upgrade_single_server_backup_control(config_root, "windows").unwrap());
        let console_after = fs::read(config_root.join("console.env")).unwrap();
        let backup_after = fs::read(config_root.join("backup.json")).unwrap();
        assert!(!upgrade_single_server_backup_control(config_root, "windows").unwrap());
        assert_eq!(
            console_after,
            fs::read(config_root.join("console.env")).unwrap()
        );
        assert_eq!(
            backup_after,
            fs::read(config_root.join("backup.json")).unwrap()
        );
        fs::write(config_root.join("backup.json"), b"{\"deployment_id\":\"c4fdfa68-d9d4-4e15-9883-344bd84c415c\",\"control\":{\"token\":\"bad\"}}").unwrap();
        assert!(upgrade_single_server_backup_control(config_root, "windows").is_err());
    }
}
