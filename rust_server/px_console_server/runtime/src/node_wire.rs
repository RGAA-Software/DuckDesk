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
                gpu_key,
            } => wire::NodeCommandAction::Start {
                port,
                launch: application(launch),
                install_root,
                gpu_key,
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
