use clap::Parser;
use std::path::PathBuf;
use std::process::Command;
use std::thread;
use std::time::Duration;
use sysinfo::System;
use windows::core::PCSTR;
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{MessageBoxA, IDYES, MB_ICONQUESTION, MB_YESNO};

#[derive(Parser, Debug)]
#[command(name = "px_uninstall")]
struct Args {
    #[arg()]
    command: Option<String>,
}

fn null_hwnd() -> Option<HWND> {
    Some(HWND(std::ptr::null_mut()))
}

fn main() {
    let args = Args::parse();

    match args.command.as_deref() {
        Some("exit") => exit_program(),
        Some("uninstall") => uninstall_program(),
        None => print_help(),
        Some(other) => {
            eprintln!("Error: Unknown command '{}'\n", other);
            print_help();
            std::process::exit(1);
        }
    }
}

fn print_help() {
    println!("Usage: px_uninstall <COMMAND>");
    println!();
    println!("Commands:");
    println!("  exit       Stop the service and exit all programs");
    println!("  uninstall  Uninstall the service and remove all programs");
    println!();
    println!("Examples:");
    println!("  px_uninstall exit");
    println!("  px_uninstall uninstall");
}

fn exit_program() {
    let text = "the software will be stopped running, are you sure?\0";
    let title = "Exit\0";
    let result = unsafe {
        MessageBoxA(
            null_hwnd(),
            PCSTR(text.as_ptr()),
            PCSTR(title.as_ptr()),
            MB_YESNO | MB_ICONQUESTION,
        )
    };
    if result != IDYES {
        return;
    }

    run_service_manager(&["stop"]);
    kill_service_process();
    kill_other_processes();
}

fn uninstall_program() {
    let text = "the software is about to be uninstalled, are you sure?\0";
    let title = "Uninstall\0";
    let result = unsafe {
        MessageBoxA(
            null_hwnd(),
            PCSTR(text.as_ptr()),
            PCSTR(title.as_ptr()),
            MB_YESNO | MB_ICONQUESTION,
        )
    };
    if result != IDYES {
        return;
    }

    run_service_manager(&["remove", "--uninstall-service"]);
    kill_service_process();
    kill_other_processes();

    std::process::exit(0);
}

fn run_service_manager(args: &[&str]) {
    let exe_dir = px_base::current_exe_dir();
    let exe_path = PathBuf::from(&exe_dir).join("px_service_manager.exe");
    if !exe_path.exists() {
        eprintln!(
            "Warning: px_service_manager.exe not found at {:?}",
            exe_path
        );
        return;
    }
    let mut cmd = Command::new(&exe_path);
    cmd.args(args);
    match cmd.spawn().and_then(|mut child| child.wait()) {
        Ok(status) => {
            if !status.success() {
                eprintln!(
                    "Warning: px_service_manager exited with code: {:?}",
                    status.code()
                );
            }
        }
        Err(e) => {
            eprintln!("Warning: Failed to run px_service_manager.exe: {}", e);
        }
    }
}

fn kill_service_process() {
    let mut sys = System::new_all();
    sys.refresh_all();

    for (_pid, process) in sys.processes() {
        if process.name() == "px_service.exe" {
            let _ = process.kill();
        }
    }

    thread::sleep(Duration::from_millis(500));
}

fn kill_other_processes() {
    let mut sys = System::new_all();
    sys.refresh_all();

    let targets = [
        "px_render.exe",
        "px_client.exe",
        "px_osinfo.exe",
        "px_function.exe",
        "px_panel.exe",
    ];

    for (_pid, process) in sys.processes() {
        let name = process.name();
        for target in &targets {
            if name == *target {
                let _ = process.kill();
            }
        }
    }
}
