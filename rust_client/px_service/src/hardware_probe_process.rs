//! Killable process boundary for synchronous driver calls, including cancellation.

use crate::hardware_probe::{ProbeKind, ProbeSnapshot};
use std::{future::Future, pin::Pin, process::Stdio, time::Duration};
use tokio::{io::AsyncReadExt, process::Command};

pub(crate) type ProbeFuture = Pin<Box<dyn Future<Output = Result<ProbeSnapshot, String>> + Send>>;

pub(crate) trait ProbeRunner: Send + Sync {
    fn sample(&self, kind: ProbeKind) -> ProbeFuture;
}

pub(crate) struct NativeProbeRunner;

impl ProbeRunner for NativeProbeRunner {
    fn sample(&self, kind: ProbeKind) -> ProbeFuture {
        Box::pin(async move {
            let executable = std::env::current_exe().map_err(|error| error.to_string())?;
            let mut command = Command::new(executable);
            command.arg("--hardware-probe").arg(kind.name());
            let output = run_process(command, kind, Duration::from_secs(5)).await?;
            decode_snapshot(kind, &output)
        })
    }
}

fn decode_snapshot(kind: ProbeKind, output: &[u8]) -> Result<ProbeSnapshot, String> {
    let snapshot: ProbeSnapshot =
        serde_json::from_slice(output).map_err(|error| error.to_string())?;
    if snapshot.kind() != kind {
        return Err("hardware worker returned a different probe kind".into());
    }
    Ok(snapshot)
}

async fn run_process(
    mut command: Command,
    kind: ProbeKind,
    deadline: Duration,
) -> Result<Vec<u8>, String> {
    const MAX_STDOUT: u64 = 64 * 1024;
    const MAX_STDERR: u64 = 8 * 1024;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    const ABOVE_NORMAL_PRIORITY: u32 = 0x8000;
    const BELOW_NORMAL_PRIORITY: u32 = 0x4000;
    command
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .creation_flags(
            CREATE_NO_WINDOW
                | if kind == ProbeKind::Gpu {
                    ABOVE_NORMAL_PRIORITY
                } else {
                    BELOW_NORMAL_PRIORITY
                },
        );
    let mut child = command.spawn().map_err(|error| error.to_string())?;
    let worker_pid = child.id();
    let stdout = child
        .stdout
        .take()
        .ok_or("hardware worker stdout missing")?;
    let stderr = child
        .stderr
        .take()
        .ok_or("hardware worker stderr missing")?;
    let mut stdout_bytes = Vec::new();
    let mut stderr_bytes = Vec::new();
    let mut stdout_reader = stdout.take(MAX_STDOUT + 1);
    let mut stderr_reader = stderr.take(MAX_STDERR + 1);
    let outcome = tokio::time::timeout(deadline, async {
        tokio::try_join!(
            child.wait(),
            stdout_reader.read_to_end(&mut stdout_bytes),
            stderr_reader.read_to_end(&mut stderr_bytes),
        )
    })
    .await;
    let status = match outcome {
        Ok(Ok((status, _, _))) => status,
        Ok(Err(error)) => return Err(format!("hardware worker output failed: {error}")),
        Err(_) => {
            child.start_kill().map_err(|error| error.to_string())?;
            tokio::time::timeout(Duration::from_secs(2), child.wait())
                .await
                .map_err(|_| "hardware worker termination was not confirmed")?
                .map_err(|error| error.to_string())?;
            tracing::warn!(
                probe = kind.name(),
                ?worker_pid,
                "hardware probe timed out; worker terminated"
            );
            return Err("hardware probe timed out".into());
        }
    };
    if stdout_bytes.len() > MAX_STDOUT as usize || stderr_bytes.len() > MAX_STDERR as usize {
        return Err("hardware worker output exceeded its limit".into());
    }
    let diagnostics = String::from_utf8_lossy(&stderr_bytes);
    if !status.success() {
        return Err(format!(
            "hardware worker exited {status}: {}",
            diagnostics.trim()
        ));
    }
    if !diagnostics.trim().is_empty() {
        tracing::warn!(probe = kind.name(), details = %diagnostics.trim(), "hardware probe returned partial diagnostics");
    }
    Ok(stdout_bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn worker_results_cannot_cross_hardware_lanes() {
        let cpu_payload =
            br#"{"kind":"cpu","snapshot":{"logical_processors":8,"utilization_per_mille":100}}"#;
        assert!(decode_snapshot(ProbeKind::Cpu, cpu_payload).is_ok());
        assert!(decode_snapshot(ProbeKind::Gpu, cpu_payload).is_err());
        assert!(decode_snapshot(
            ProbeKind::Cpu,
            br#"{"kind":"cpu","snapshot":{"unexpected":1}}"#
        )
        .is_err());
    }

    #[tokio::test]
    async fn cancellation_terminates_the_exact_native_worker() {
        use std::os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle};
        use windows::Win32::Foundation::{HANDLE, WAIT_OBJECT_0};
        use windows::Win32::System::Threading::{
            OpenProcess, WaitForSingleObject, PROCESS_SYNCHRONIZE,
        };

        let fixture_directory = tempfile::tempdir().unwrap();
        let pid_file = fixture_directory.path().join("probe-pid");
        let escaped_path = pid_file.display().to_string().replace('\'', "''");
        let mut worker_command =
            Command::new("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
        worker_command.args(["-NoProfile", "-NonInteractive", "-Command"]);
        worker_command.arg(format!("[IO.File]::WriteAllText('{escaped_path}', [string]$PID); [Threading.Thread]::Sleep(30000)"));
        let worker_task = tokio::spawn(run_process(
            worker_command,
            ProbeKind::Disk,
            Duration::from_secs(30),
        ));
        let worker_pid = tokio::time::timeout(Duration::from_secs(5), async {
            loop {
                if let Ok(contents) = std::fs::read_to_string(&pid_file) {
                    if let Ok(worker_pid) = contents.parse::<u32>() {
                        break worker_pid;
                    }
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .unwrap();
        let process_handle =
            unsafe { OpenProcess(PROCESS_SYNCHRONIZE, false, worker_pid) }.unwrap();
        let observed_process = unsafe { OwnedHandle::from_raw_handle(process_handle.0) };
        worker_task.abort();
        assert!(worker_task.await.unwrap_err().is_cancelled());
        assert_eq!(
            unsafe { WaitForSingleObject(HANDLE(observed_process.as_raw_handle()), 2000) },
            WAIT_OBJECT_0
        );
    }

    #[tokio::test]
    async fn synchronous_worker_timeout_terminates_and_allows_the_next_probe() {
        let mut slow_worker =
            Command::new("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
        slow_worker.args([
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            "[Threading.Thread]::Sleep(30000)",
        ]);
        let started = std::time::Instant::now();
        let error = run_process(slow_worker, ProbeKind::Cpu, Duration::from_millis(150))
            .await
            .unwrap_err();
        assert!(error.contains("timed out"));
        assert!(started.elapsed() < Duration::from_secs(3));
        let mut next_worker = Command::new("C:\\Windows\\System32\\cmd.exe");
        next_worker.args(["/d", "/c", "echo recovered"]);
        let output = run_process(next_worker, ProbeKind::Cpu, Duration::from_secs(2))
            .await
            .unwrap();
        assert_eq!(String::from_utf8(output).unwrap().trim(), "recovered");
    }
}
