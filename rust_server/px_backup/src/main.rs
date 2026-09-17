mod restore_command;
#[cfg(windows)]
mod windows_service;

use px_backup::{BackupDaemon, BackupDaemonConfig, PinnedPgTools};
use std::{
    env,
    path::PathBuf,
    process::ExitCode,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

#[cfg(unix)]
static UNIX_STOPPING: AtomicBool = AtomicBool::new(false);
#[cfg(unix)]
static UNIX_CANCELLATION: std::sync::OnceLock<px_backup::BackupCancellation> =
    std::sync::OnceLock::new();
#[cfg(windows)]
static CONSOLE_STOPPING: std::sync::OnceLock<Arc<AtomicBool>> = std::sync::OnceLock::new();
#[cfg(windows)]
static CONSOLE_CANCELLATION: std::sync::OnceLock<px_backup::BackupCancellation> =
    std::sync::OnceLock::new();

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("px_backup: {message}");
            ExitCode::from(match message {
                "configuration rejected" => 10,
                "Windows service dispatcher rejected startup" => 11,
                "service configuration already initialized"
                | "service identity already initialized" => 12,
                "restore remains RecoveryRequired" => 20,
                "restore approval rejected" | "restore approval request rejected" => 21,
                "restore configuration rejected"
                | "restore execution configuration rejected"
                | "restore execution tool identity rejected"
                | "restore execution failed closed"
                | "restore execution report rejected"
                | "restore provision configuration rejected"
                | "restore provision tool identity rejected"
                | "restore operator provisioning failed closed"
                | "write barrier configuration rejected"
                | "write barrier tool identity rejected"
                | "write barrier acquisition failed closed"
                | "write barrier release requires reconciliation"
                | "recovery seal configuration rejected"
                | "recovery seal tool identity rejected"
                | "recovery seal failed closed"
                | "recovery seal evidence rejected"
                | "recovery witness configuration rejected"
                | "recovery witness store rejected"
                | "recovery witness recording failed closed"
                | "external recovery witness rejected"
                | "restore admission store rejected"
                | "restore evidence rejected"
                | "backup repository rejected restore access"
                | "recovery set is unavailable"
                | "restore result serialization failed" => 22,
                _ => 1,
            })
        }
    }
}

fn run() -> Result<(), &'static str> {
    let arguments = env::args_os().collect::<Vec<_>>();
    if arguments.len() < 3 {
        return Err("usage: px_backup run|service|barrier-acquire|barrier-release|witness-record|restore-provision|restore-execute|restore-seal|restore-evaluate|restore-approve <private-config-path> [private-approval-path]");
    }
    let command = arguments[1]
        .to_str()
        .ok_or("backup daemon command is invalid")?;
    let config_path = PathBuf::from(&arguments[2]);
    match command {
        "run" if arguments.len() == 3 => run_interactive(config_path),
        #[cfg(windows)]
        "service" if arguments.len() == 3 => windows_service::dispatch(config_path),
        #[cfg(not(windows))]
        "service" if arguments.len() == 3 => {
            Err("service mode is available only through Windows SCM")
        }
        "restore-evaluate" if arguments.len() == 3 => restore_command::evaluate(config_path),
        "restore-execute" if arguments.len() == 3 => run_restore_execute(config_path),
        "restore-provision" if arguments.len() == 3 => run_restore_provision(config_path),
        "restore-seal" if arguments.len() == 3 => run_restore_seal(config_path),
        "witness-record" if arguments.len() == 3 => {
            restore_command::record_recovery_witness(config_path)
        }
        "barrier-acquire" if arguments.len() == 3 => run_barrier_acquire(config_path),
        "barrier-release" if arguments.len() == 3 => run_barrier_release(config_path),
        "restore-approve" if arguments.len() == 4 => {
            restore_command::approve(config_path, PathBuf::from(&arguments[3]))
        }
        _ => Err("usage: px_backup run|service|barrier-acquire|barrier-release|witness-record|restore-provision|restore-execute|restore-seal|restore-evaluate|restore-approve <private-config-path> [private-approval-path]"),
    }
}

fn run_restore_execute(config_path: PathBuf) -> Result<(), &'static str> {
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = px_backup::BackupCancellation::default();
    install_console_shutdown(stopping, cancellation.clone())?;
    restore_command::execute(config_path, cancellation)
}

fn run_restore_provision(config_path: PathBuf) -> Result<(), &'static str> {
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = px_backup::BackupCancellation::default();
    install_console_shutdown(stopping, cancellation.clone())?;
    restore_command::provision(config_path, cancellation)
}

fn run_restore_seal(config_path: PathBuf) -> Result<(), &'static str> {
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = px_backup::BackupCancellation::default();
    install_console_shutdown(stopping, cancellation.clone())?;
    restore_command::seal_recovery(config_path, cancellation)
}

fn run_barrier_acquire(config_path: PathBuf) -> Result<(), &'static str> {
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = px_backup::BackupCancellation::default();
    install_console_shutdown(stopping, cancellation.clone())?;
    restore_command::acquire_write_barrier(config_path, cancellation)
}

fn run_barrier_release(config_path: PathBuf) -> Result<(), &'static str> {
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = px_backup::BackupCancellation::default();
    install_console_shutdown(stopping, cancellation.clone())?;
    restore_command::release_write_barrier(config_path, cancellation)
}

fn run_interactive(config_path: PathBuf) -> Result<(), &'static str> {
    let config =
        BackupDaemonConfig::load_private(&config_path).map_err(|_| "configuration rejected")?;
    let daemon = BackupDaemon::open(config, now_unix()?).map_err(|_| "startup rejected")?;
    let stopping = Arc::new(AtomicBool::new(false));
    let cancellation = daemon.cancellation();
    install_console_shutdown(Arc::clone(&stopping), cancellation)?;
    run_daemon(daemon, stopping)
}

fn run_daemon(
    mut daemon: BackupDaemon<PinnedPgTools>,
    stopping: Arc<AtomicBool>,
) -> Result<(), &'static str> {
    while !stop_requested(&stopping) {
        daemon
            .run_due(now_unix()?)
            .map_err(|_| "backup daemon failed closed")?;
        let poll_interval = daemon.poll_interval();
        let mut elapsed = Duration::ZERO;
        while elapsed < poll_interval && !stop_requested(&stopping) {
            let sleep_duration = (poll_interval - elapsed).min(Duration::from_millis(100));
            thread::sleep(sleep_duration);
            elapsed += sleep_duration;
        }
    }
    Ok(())
}

fn stop_requested(stopping: &AtomicBool) -> bool {
    if stopping.load(Ordering::Acquire) {
        return true;
    }
    #[cfg(unix)]
    {
        UNIX_STOPPING.load(Ordering::Acquire)
    }
    #[cfg(windows)]
    {
        false
    }
}

#[cfg(unix)]
fn install_console_shutdown(
    _stopping: Arc<AtomicBool>,
    cancellation: px_backup::BackupCancellation,
) -> Result<(), &'static str> {
    UNIX_CANCELLATION
        .set(cancellation)
        .map_err(|_| "shutdown handler already initialized")?;
    let interrupt_result = unsafe { libc::signal(libc::SIGINT, unix_signal_handler as usize) };
    let terminate_result = unsafe { libc::signal(libc::SIGTERM, unix_signal_handler as usize) };
    if interrupt_result == libc::SIG_ERR || terminate_result == libc::SIG_ERR {
        return Err("shutdown handler unavailable");
    }
    Ok(())
}

#[cfg(unix)]
extern "C" fn unix_signal_handler(_signal_number: libc::c_int) {
    UNIX_STOPPING.store(true, Ordering::Release);
    if let Some(cancellation) = UNIX_CANCELLATION.get() {
        cancellation.cancel();
    }
}

#[cfg(windows)]
fn install_console_shutdown(
    stopping: Arc<AtomicBool>,
    cancellation: px_backup::BackupCancellation,
) -> Result<(), &'static str> {
    use windows::Win32::System::Console::SetConsoleCtrlHandler;
    CONSOLE_STOPPING
        .set(stopping)
        .map_err(|_| "shutdown handler already initialized")?;
    CONSOLE_CANCELLATION
        .set(cancellation)
        .map_err(|_| "shutdown cancellation already initialized")?;
    unsafe { SetConsoleCtrlHandler(Some(windows_console_handler), true) }
        .map_err(|_| "shutdown handler unavailable")
}

#[cfg(windows)]
unsafe extern "system" fn windows_console_handler(control_type: u32) -> windows::core::BOOL {
    use windows::Win32::System::Console::{
        CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT, CTRL_C_EVENT, CTRL_LOGOFF_EVENT, CTRL_SHUTDOWN_EVENT,
    };
    if matches!(
        control_type,
        CTRL_C_EVENT
            | CTRL_BREAK_EVENT
            | CTRL_CLOSE_EVENT
            | CTRL_LOGOFF_EVENT
            | CTRL_SHUTDOWN_EVENT
    ) {
        if let Some(stopping) = CONSOLE_STOPPING.get() {
            stopping.store(true, Ordering::Release);
        }
        if let Some(cancellation) = CONSOLE_CANCELLATION.get() {
            cancellation.cancel();
        }
        return windows::core::BOOL(1);
    }
    windows::core::BOOL(0)
}

fn now_unix() -> Result<u64, &'static str> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|_| "system clock unavailable")
}
