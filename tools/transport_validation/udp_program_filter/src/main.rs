//! Temporary UDP denial scoped to one isolated transport-validation executable.
//! WFP dynamic-session objects disappear even if this helper is interrupted.
use std::{
    ffi::c_void,
    fs::{self, OpenOptions},
    io::{self, Write},
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    ptr::{self, NonNull},
    thread,
    time::{Duration, Instant},
};
use windows_sys::Win32::NetworkManagement::WindowsFilteringPlatform::*;

type ValidationResult<T> = Result<T, Box<dyn std::error::Error>>;

fn check_status(operation: &str, status: u32) -> ValidationResult<()> {
    if status != 0 {
        return Err(format!("{operation} failed: 0x{status:08x}").into());
    }
    Ok(())
}

struct FilterEngine(NonNull<c_void>);

impl FilterEngine {
    fn open() -> ValidationResult<Self> {
        let session = FWPM_SESSION0 {
            flags: FWPM_SESSION_FLAG_DYNAMIC,
            ..Default::default()
        };
        let mut engine_handle = ptr::null_mut();
        // The ABI writes an owned engine handle; the session's lifetime is this guard.
        check_status("FwpmEngineOpen0", unsafe {
            FwpmEngineOpen0(ptr::null(), 10, ptr::null(), &session, &mut engine_handle)
        })?;
        Ok(Self(
            NonNull::new(engine_handle).ok_or("Empty WFP engine handle")?,
        ))
    }
}

impl Drop for FilterEngine {
    fn drop(&mut self) {
        // Closing a dynamic engine session removes only this session's filters.
        unsafe { FwpmEngineClose0(self.0.as_ptr()) };
    }
}

struct FilterAllocation<T>(NonNull<T>);

impl<T> Drop for FilterAllocation<T> {
    fn drop(&mut self) {
        let mut allocation = self.0.as_ptr().cast::<c_void>();
        // WFP allocations must be released through the matching allocator.
        unsafe { FwpmFreeMemory0(&mut allocation) };
    }
}

fn add_udp_filters(engine: &FilterEngine, program: &Path) -> ValidationResult<Vec<u64>> {
    let program_name: Vec<u16> = program.as_os_str().encode_wide().chain(Some(0)).collect();
    let mut application_blob = ptr::null_mut();
    check_status("FwpmGetAppIdFromFileName0", unsafe {
        FwpmGetAppIdFromFileName0(program_name.as_ptr(), &mut application_blob)
    })?;
    let application =
        FilterAllocation(NonNull::new(application_blob).ok_or("Empty application identity")?);
    let mut conditions = [FWPM_FILTER_CONDITION0::default(); 2];
    conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0].matchType = FWP_MATCH_EQUAL;
    conditions[0].conditionValue.r#type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.Anonymous.byteBlob = application.0.as_ptr();
    conditions[1].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
    conditions[1].matchType = FWP_MATCH_EQUAL;
    conditions[1].conditionValue.r#type = FWP_UINT8;
    conditions[1].conditionValue.Anonymous.uint8 = 17;
    let mut display_name: Vec<u16> = "Pixels owned transport UDP validation"
        .encode_utf16()
        .chain(Some(0))
        .collect();
    let mut filter_ids = Vec::new();
    for layer in [
        FWPM_LAYER_ALE_AUTH_CONNECT_V4,
        FWPM_LAYER_ALE_AUTH_CONNECT_V6,
        FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4,
        FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6,
    ] {
        let mut filter = FWPM_FILTER0 {
            layerKey: layer,
            subLayerKey: FWPM_SUBLAYER_UNIVERSAL,
            flags: FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT,
            numFilterConditions: conditions.len() as u32,
            filterCondition: conditions.as_mut_ptr(),
            ..Default::default()
        };
        filter.displayData.name = display_name.as_mut_ptr();
        filter.weight.r#type = FWP_EMPTY;
        filter.action.r#type = FWP_ACTION_BLOCK;
        let mut filter_id = 0;
        // Input buffers are borrowed only for this synchronous ABI call; WFP copies them.
        check_status("FwpmFilterAdd0", unsafe {
            FwpmFilterAdd0(engine.0.as_ptr(), &filter, ptr::null_mut(), &mut filter_id)
        })?;
        filter_ids.push(filter_id);
    }
    Ok(filter_ids)
}

fn run() -> ValidationResult<()> {
    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    if arguments.len() != 3 {
        return Err("Expected: <isolated probe path> <new report path> <maximum seconds>".into());
    }
    let program = fs::canonicalize(PathBuf::from(&arguments[0]))?;
    let normalized = program
        .to_string_lossy()
        .replace('/', "\\")
        .to_ascii_lowercase();
    if ![
        "\\unified-acceptance-20261013\\t4-tools\\px_transport_probe.exe",
        "\\unified-acceptance-20261013\\t4-tools\\px_nat_probe.exe",
    ]
    .iter()
    .any(|allowed_suffix| normalized.ends_with(allowed_suffix))
    {
        return Err("Only the isolated acceptance probe is permitted".into());
    }
    let requested_report = PathBuf::from(&arguments[1]);
    let report_directory = fs::canonicalize(
        requested_report
            .parent()
            .ok_or("Missing report directory")?,
    )?;
    if Some(report_directory.as_path()) != program.parent() {
        return Err("Report must belong to the isolated probe directory".into());
    }
    let report_path = report_directory.join(
        requested_report
            .file_name()
            .ok_or("Missing report filename")?,
    );
    let maximum_seconds: u64 = arguments[2]
        .to_str()
        .ok_or("Invalid duration encoding")?
        .parse()?;
    if !(60..=900).contains(&maximum_seconds) {
        return Err("Maximum seconds must be 60..900".into());
    }
    let stop_path = report_path.with_extension("stop");
    if stop_path.exists() {
        return Err("Preserve existing stop marker".into());
    }
    let mut report_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&report_path)?;
    let engine = FilterEngine::open()?;
    let filter_ids = add_udp_filters(&engine, &program)?;
    let started = Instant::now();
    let ready = serde_json::json!({"state":"active","pid":std::process::id(),"program":program,"filter_ids":filter_ids,"maximum_seconds":maximum_seconds,"dynamic_session":true,"protocol":17,"ipv4_ipv6":true});
    writeln!(report_file, "{ready}")?;
    report_file.flush()?;
    println!("{ready}");
    io::stdout().flush()?;
    while started.elapsed().as_secs() < maximum_seconds && !stop_path.exists() {
        thread::sleep(Duration::from_millis(250));
    }
    let requested_stop = stop_path.exists();
    drop(engine);
    let inspection = FilterEngine::open()?;
    let mut remaining_ids = Vec::new();
    for filter_id in &filter_ids {
        let mut remaining_filter = ptr::null_mut();
        let status =
            unsafe { FwpmFilterGetById0(inspection.0.as_ptr(), *filter_id, &mut remaining_filter) };
        if status == 0 {
            let allocation =
                FilterAllocation(NonNull::new(remaining_filter).ok_or("Empty retained filter")?);
            remaining_ids.push(*filter_id);
            drop(allocation);
        } else if status != 0x80320003 {
            return Err(format!("Cleanup query failed: 0x{status:08x}").into());
        }
    }
    let closed = serde_json::json!({"state":"closed","elapsed_seconds":started.elapsed().as_secs_f64(),"requested_stop":requested_stop,"remaining_filter_ids":remaining_ids});
    writeln!(report_file, "{closed}")?;
    println!("{closed}");
    if !remaining_ids.is_empty() {
        return Err("Owned filters remained after session close".into());
    }
    Ok(())
}

fn main() -> ValidationResult<()> {
    run()
}
