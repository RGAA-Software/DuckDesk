use clap::Parser;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use sysinfo::System;
use windows::core::PCSTR;
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{
    MessageBoxA, IDYES, MB_ICONQUESTION, MB_YESNO,
};

#[derive(Parser, Debug)]
#[command(name = "GammaRayUninstall")]
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
    println!("Usage: GammaRayUninstall <COMMAND>");
    println!();
    println!("Commands:");
    println!("  exit       Stop the service and exit all programs");
    println!("  uninstall  Uninstall the service and remove all programs");
    println!();
    println!("Examples:");
    println!("  GammaRayUninstall exit");
    println!("  GammaRayUninstall uninstall");
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
    kill_processes();
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

    run_service_manager(&["remove", "--uninstall-service", "true"]);
    kill_processes();

    let exe_dir = gr_base::current_exe_dir();
    let shadow_deleter = PathBuf::from(&exe_dir).join("shadow_deleter.exe");
    let _ = Command::new(&shadow_deleter).arg(&shadow_deleter).spawn();

    std::process::exit(0);
}

fn run_service_manager(args: &[&str]) {
    let exe_dir = gr_base::current_exe_dir();
    let exe_path = PathBuf::from(&exe_dir).join("GammaRayServiceManager.exe");
    let mut cmd = Command::new(&exe_path);
    cmd.args(args)
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let _ = cmd.spawn().and_then(|mut child| child.wait());
}

fn kill_processes() {
    let mut sys = System::new_all();
    sys.refresh_all();

    let targets = [
        "GammaRayGuard.exe",
        "GammaRaySysInfo.exe",
        "GammaRay.exe",
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
