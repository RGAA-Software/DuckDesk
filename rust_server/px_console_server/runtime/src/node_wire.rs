use px_console_store as store;
use px_node_protocol as wire;
pub use px_node_protocol::{NodeRequest, NodeResponse, MAX_CONNECTIONS, MAX_MESSAGE_BYTES};

pub fn report(value: wire::NodeReport) -> store::NodeReport {
    store::NodeReport {
        sequence: value.sequence,
        product_version_code: value.product_version_code,
        public_host: value.public_host,
        desktop_port: value.desktop_port,
        application_port_start: value.application_port_start,
        application_port_end: value.application_port_end,
        game_hook: value.game_hook,
        webview: value.webview,
        rdp: value.rdp,
        telemetry: telemetry(value.telemetry),
    }
}

pub fn telemetry_backfill(
    samples: Vec<wire::TelemetryBackfillSample>,
) -> Vec<store::NodeTelemetryBackfillSample> {
    samples
        .into_iter()
        .map(|sample| store::NodeTelemetryBackfillSample {
            sample_id: sample.sample_id,
            telemetry: telemetry(sample.telemetry),
        })
        .collect()
}

fn telemetry(value: wire::NodeTelemetry) -> store::NodeTelemetry {
    store::NodeTelemetry {
        sampled_at: value.sampled_at,
        probe_state: match value.probe_state {
            wire::TelemetryProbeState::Ready => store::TelemetryProbeState::Ready,
            wire::TelemetryProbeState::Partial => store::TelemetryProbeState::Partial,
            wire::TelemetryProbeState::Unavailable => store::TelemetryProbeState::Unavailable,
        },
        logical_processors: value.logical_processors,
        cpu_utilization_per_mille: value.cpu_utilization_per_mille,
        memory_total_bytes: value.memory_total_bytes,
        memory_available_bytes: value.memory_available_bytes,
        disk_total_bytes: value.disk_total_bytes,
        disk_free_bytes: value.disk_free_bytes,
        gpu_inventory_revision: value.gpu_inventory_revision,
        gpus: value
            .gpus
            .into_iter()
            .map(|gpu| store::NodeGpuTelemetry {
                stable_key: gpu.stable_key,
                name: gpu.name,
                runtime_binding_ready: gpu.runtime_binding_ready,
                dedicated_memory_bytes: gpu.dedicated_memory_bytes,
                used_memory_bytes: gpu.used_memory_bytes,
                utilization_per_mille: gpu.utilization_per_mille,
                encoder_utilization_per_mille: gpu.encoder_utilization_per_mille,
            })
            .collect(),
    }
}

pub fn inventory(value: wire::RuntimeInventory) -> store::RuntimeInventory {
    store::RuntimeInventory {
        challenge_id: value.challenge_id,
        runtimes: value
            .runtimes
            .into_iter()
            .map(|runtime| store::ObservedRuntime {
                instance_id: runtime.instance_id,
                launch_id: runtime.launch_id,
                port: runtime.port,
                phase: match runtime.phase {
                    wire::ObservedRuntimePhase::Starting => store::ObservedRuntimePhase::Starting,
                    wire::ObservedRuntimePhase::Running => store::ObservedRuntimePhase::Running,
                },
            })
            .collect(),
    }
}

pub fn receipt(value: wire::CommandReceipt) -> store::CommandReceipt {
    store::CommandReceipt {
        command_id: value.command_id,
        lease_id: value.lease_id,
        instance_id: value.instance_id,
        launch_id: value.launch_id,
        instance_revision: value.instance_revision,
        outcome: match value.outcome {
            wire::CommandOutcome::Running { port } => store::CommandOutcome::Running { port },
            wire::CommandOutcome::Absent => store::CommandOutcome::Absent,
            wire::CommandOutcome::Unknown => store::CommandOutcome::Unknown,
        },
    }
}

pub fn observation(value: wire::DeploymentObservation) -> store::DeploymentObservation {
    store::DeploymentObservation {
        deployment_revision: value.deployment_revision,
        application_revision: value.application_revision,
        endpoint_revision: value.endpoint_revision,
        sequence: value.sequence,
        status: match value.status {
            wire::PreparationState::Pending => store::PreparationState::Pending,
            wire::PreparationState::Ready => store::PreparationState::Ready,
            wire::PreparationState::Failed { reason } => store::PreparationState::Failed {
                reason: match reason {
                    wire::PreparationFailure::MissingFiles => {
                        store::PreparationFailure::MissingFiles
                    }
                    wire::PreparationFailure::UnsupportedMode => {
                        store::PreparationFailure::UnsupportedMode
                    }
                    wire::PreparationFailure::BindingUnverified => {
                        store::PreparationFailure::BindingUnverified
                    }
                    wire::PreparationFailure::InvalidConfiguration => {
                        store::PreparationFailure::InvalidConfiguration
                    }
                    wire::PreparationFailure::DependencyUnavailable => {
                        store::PreparationFailure::DependencyUnavailable
                    }
                },
            },
        },
    }
}

pub fn assignment(value: store::NodeDeploymentAssignment) -> wire::DeploymentAssignment {
    wire::DeploymentAssignment {
        id: value.id,
        application_id: value.application_id,
        deployment_revision: value.deployment_revision,
        application_revision: value.application_revision,
        disabled: value.disabled,
        preparation: match value.preparation {
            store::NodeDeploymentPreparation::GameHook {
                install_root,
                executable_relative,
                gpu_key,
            } => wire::DeploymentPreparation::GameHook {
                install_root,
                executable_relative,
                gpu_key,
            },
            store::NodeDeploymentPreparation::Webview { gpu_key } => {
                wire::DeploymentPreparation::Webview { gpu_key }
            }
            store::NodeDeploymentPreparation::Rdp { gpu_key } => {
                wire::DeploymentPreparation::Rdp { gpu_key }
            }
        },
    }
}

pub fn challenge(value: store::ReconciliationChallenge) -> wire::ReconciliationChallenge {
    wire::ReconciliationChallenge {
        id: value.id,
        node_generation: value.node_generation,
        control_epoch: value.control_epoch,
        deadline: value.deadline,
        launches: value
            .launches
            .into_iter()
            .map(|launch| wire::ExpectedLaunch {
                instance_id: launch.instance_id,
                launch_id: launch.launch_id,
                reject_through_revision: launch.reject_through_revision,
                port: launch.port,
                desired_state: launch.desired_state,
            })
            .collect(),
    }
}

pub fn frontend_grant(value: store::FrontendGrant) -> wire::FrontendGrant {
    wire::FrontendGrant {
        session_id: value.session.id,
        revision: value.session.revision,
        target: frontend_target(value.session.target),
        client_type: value.session.client_type,
        access_role: value.session.access_role,
        valid_for_ms: value.valid_for_ms,
    }
}

pub fn expected_frontend(value: store::ExpectedFrontend) -> wire::ExpectedFrontend {
    wire::ExpectedFrontend {
        id: value.id,
        revision: value.revision,
        state: value.state,
    }
}

pub fn frontend_retirement(value: store::FrontendRetirement) -> wire::FrontendRetirement {
    wire::FrontendRetirement {
        session_id: value.session_id,
        challenge_id: value.challenge_id,
        reject_through_revision: value.reject_through_revision,
        deadline: value.deadline,
    }
}

pub fn open_channel(value: wire::OpenChannel) -> store::OpenChannel {
    store::OpenChannel {
        source_id: value.source_id,
        session_id: value.session_id,
        kind: match value.kind {
            wire::ChannelKind::Control => store::ChannelKind::Control,
            wire::ChannelKind::Media => store::ChannelKind::Media,
            wire::ChannelKind::Audio => store::ChannelKind::Audio,
            wire::ChannelKind::File => store::ChannelKind::File,
            wire::ChannelKind::Rdp => store::ChannelKind::Rdp,
        },
    }
}

pub fn channel_progress(value: wire::ChannelProgress) -> store::ChannelProgress {
    store::ChannelProgress {
        sequence: value.sequence,
        sent_bytes: value.sent_bytes,
        received_bytes: value.received_bytes,
        elapsed_ms: value.elapsed_ms,
        outcome: match value.outcome {
            wire::ChannelOutcome::Progress => store::ChannelOutcome::Progress,
            wire::ChannelOutcome::Closed { reason } => store::ChannelOutcome::Closed {
                reason: match reason {
                    wire::ChannelClose::PeerClosed => store::ChannelClose::PeerClosed,
                    wire::ChannelClose::UserStopped => store::ChannelClose::UserStopped,
                },
            },
            wire::ChannelOutcome::Failed { reason } => store::ChannelOutcome::Failed {
                reason: match reason {
                    wire::ChannelFailure::TransportLost => store::ChannelFailure::TransportLost,
                    wire::ChannelFailure::PolicyRevoked => store::ChannelFailure::PolicyRevoked,
                    wire::ChannelFailure::IoError => store::ChannelFailure::IoError,
                },
            },
        },
    }
}

pub fn begin_file_transfer(value: wire::BeginFileTransfer) -> store::BeginFileTransfer {
    store::BeginFileTransfer {
        request_id: value.transfer_request_id,
        session_id: value.session_id,
        direction: match value.direction {
            wire::TransferDirection::ToNode => store::TransferDirection::ToNode,
            wire::TransferDirection::FromNode => store::TransferDirection::FromNode,
        },
        file_name: value.file_name,
        total_bytes: value.total_bytes,
        expected_sha256: value.expected_sha256,
    }
}

pub fn transfer_progress(value: wire::TransferProgress) -> store::TransferProgress {
    store::TransferProgress {
        sequence: value.sequence,
        transferred_bytes: value.transferred_bytes,
        outcome: match value.outcome {
            wire::TransferOutcome::Progress => store::TransferOutcome::Progress,
            wire::TransferOutcome::Completed { received_sha256 } => {
                store::TransferOutcome::Completed { received_sha256 }
            }
            wire::TransferOutcome::Failed { reason } => store::TransferOutcome::Failed {
                reason: match reason {
                    wire::TransferFailure::TransportLost => store::TransferFailure::TransportLost,
                    wire::TransferFailure::HashMismatch => store::TransferFailure::HashMismatch,
                    wire::TransferFailure::PolicyRevoked => store::TransferFailure::PolicyRevoked,
                    wire::TransferFailure::IoError => store::TransferFailure::IoError,
                    wire::TransferFailure::SourceChanged => store::TransferFailure::SourceChanged,
                },
            },
            wire::TransferOutcome::Cancelled => store::TransferOutcome::Cancelled,
        },
    }
}

pub fn recording_report(value: wire::RecordingReport) -> store::RecordingReport {
    store::RecordingReport {
        source_id: value.source_id,
        source_sha256: value.source_sha256,
        session_id: value.session_id,
        file_name: value.file_name,
        size_bytes: value.size_bytes,
        modified_unix_ms: value.modified_unix_ms,
        codec: match value.codec {
            wire::RecordingCodec::H264 => store::RecordingCodec::H264,
            wire::RecordingCodec::H265 => store::RecordingCodec::H265,
            wire::RecordingCodec::Av1 => store::RecordingCodec::Av1,
            wire::RecordingCodec::Unknown => store::RecordingCodec::Unknown,
        },
        sequence: value.sequence,
        present: value.present,
    }
}

fn frontend_target(value: store::SessionTarget) -> wire::FrontendTarget {
    match value {
        store::SessionTarget::Desktop { device_id } => wire::FrontendTarget::Desktop { device_id },
        store::SessionTarget::CloudApplication {
            application_id,
            instance_id,
        } => wire::FrontendTarget::CloudApplication {
            application_id,
            instance_id,
        },
    }
}

pub fn command(value: store::NodeCommand) -> wire::NodeCommand {
    wire::NodeCommand {
        id: value.id,
        instance_id: value.instance_id,
        launch_id: value.launch_id,
        application_id: value.application_id,
        deployment_id: value.deployment_id,
        application_revision: value.application_revision,
        deployment_revision: value.deployment_revision,
        instance_revision: value.instance_revision,
        node_generation: value.node_generation,
        control_epoch: value.control_epoch,
        endpoint_revision: value.endpoint_revision,
        lease_id: value.lease_id,
        lease_until: value.lease_until,
        deadline: value.deadline,
        action: match value.action {
            store::NodeCommandAction::Start {
                port,
                launch,
                install_root,
                gpu_reservation,
            } => wire::NodeCommandAction::Start {
                port,
                launch: application(launch),
                install_root,
                gpu_reservation: gpu_reservation.map(|reservation| wire::GpuReservation {
                    stable_key: reservation.stable_key,
                    inventory_revision: reservation.inventory_revision,
                    memory_bytes: reservation.memory_bytes,
                    compute_per_mille: reservation.compute_per_mille,
                    encoder_per_mille: reservation.encoder_per_mille,
                    memory_reserve_bytes: reservation.memory_reserve_bytes,
                    compute_limit_per_mille: reservation.compute_limit_per_mille,
                    encoder_limit_per_mille: reservation.encoder_limit_per_mille,
                }),
            },
            store::NodeCommandAction::Stop => wire::NodeCommandAction::Stop,
        },
    }
}

fn application(value: store::ApplicationLaunch) -> wire::ApplicationLaunch {
    match value {
        store::ApplicationLaunch::GameHook {
            executable_relative,
            arguments,
            video,
        } => wire::ApplicationLaunch::GameHook {
            executable_relative,
            arguments,
            video: video_spec(video),
        },
        store::ApplicationLaunch::Webview { entry_url, video } => {
            wire::ApplicationLaunch::Webview {
                entry_url,
                video: video_spec(video),
            }
        }
        store::ApplicationLaunch::Rdp => wire::ApplicationLaunch::Rdp,
    }
}

fn video_spec(value: store::VideoSpec) -> wire::VideoSpec {
    wire::VideoSpec {
        codec: match value.codec {
            store::VideoCodec::H264 => wire::VideoCodec::H264,
            store::VideoCodec::H265 => wire::VideoCodec::H265,
        },
        bitrate_kbps: value.bitrate_kbps,
    }
}
