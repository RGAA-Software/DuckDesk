use std::time::Duration;

use px_node_protocol::RenderIrohEndpoint;
use uuid::Uuid;

use crate::service_host::ServiceRuntime;

pub(crate) fn apply_render_configuration(
    arguments: &mut Vec<String>,
    environment: &mut std::collections::BTreeMap<String, String>,
    configuration: Option<&px_node_protocol::IrohNetworkConfig>,
) -> Result<(), String> {
    let Some(configuration) = configuration else {
        return Ok(());
    };
    if !configuration.is_valid() {
        return Err("invalid Console iroh network configuration".into());
    }
    let encoded = serde_json::to_string(configuration).map_err(|error| error.to_string())?;
    crate::service_host::strip_service_owned_relay_arguments(arguments);
    arguments.push("--relay_enabled=false".into());
    environment.remove("PIXELS_RENDER_RELAY_TICKET");
    environment.insert("PIXELS_RENDER_IROH_CONFIGURATION".into(), encoded);
    Ok(())
}

impl ServiceRuntime {
    pub(crate) fn iroh_endpoint_snapshot(&self) -> Vec<RenderIrohEndpoint> {
        let applications = self.app_registry.list();
        let mut endpoints = Vec::new();
        for (render_name, (received_at, description)) in &self.render_iroh_descriptions {
            if received_at.elapsed() > Duration::from_secs(5)
                || !self.render_senders.contains_key(render_name)
            {
                continue;
            }
            let Some(port) = render_name
                .strip_prefix("render_")
                .and_then(|port| port.parse::<u16>().ok())
            else {
                continue;
            };
            let (instance_id, launch_id) = if port == self.config.node.network.desktop_port {
                (None, None)
            } else {
                let Some(application) = applications
                    .iter()
                    .find(|application| application.listen_port == port && application.is_active())
                else {
                    continue;
                };
                let (Ok(instance_id), Ok(launch_id)) = (
                    Uuid::parse_str(&application.instance_id),
                    Uuid::parse_str(&application.request_id),
                ) else {
                    continue;
                };
                (Some(instance_id), Some(launch_id))
            };
            endpoints.push(RenderIrohEndpoint {
                port,
                instance_id,
                launch_id,
                description: description.clone(),
            });
        }
        endpoints.sort_by_key(|endpoint| endpoint.port);
        endpoints
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn iroh_launch_removes_old_relay_credentials_and_preserves_other_launch_values() {
        let mut arguments = vec![
            "--relay_server_host=old-relay.test".into(),
            "--relay_server_port".into(),
            "4605".into(),
            "--relay_enabled=true".into(),
            "--network_listen_port=4613".into(),
        ];
        let mut environment = std::collections::BTreeMap::from([
            (
                "PIXELS_RENDER_RELAY_TICKET".into(),
                "old-relay-value".into(),
            ),
            (
                "PIXELS_RENDER_SERVICE_IPC_TOKEN".into(),
                "existing-ipc".into(),
            ),
        ]);
        let configuration = px_node_protocol::IrohNetworkConfig::default();
        apply_render_configuration(&mut arguments, &mut environment, Some(&configuration)).unwrap();
        assert_eq!(
            arguments,
            ["--network_listen_port=4613", "--relay_enabled=false"]
        );
        assert!(!environment.contains_key("PIXELS_RENDER_RELAY_TICKET"));
        assert_eq!(
            environment["PIXELS_RENDER_SERVICE_IPC_TOKEN"],
            "existing-ipc"
        );
        assert_eq!(
            serde_json::from_str::<px_node_protocol::IrohNetworkConfig>(
                &environment["PIXELS_RENDER_IROH_CONFIGURATION"]
            )
            .unwrap(),
            configuration
        );
    }
}
