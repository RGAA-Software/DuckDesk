use std::time::Duration;

use px_node_protocol::RenderIrohEndpoint;
use uuid::Uuid;

use crate::service_host::ServiceRuntime;

/// Retention is scoped to this Render's reported home, never another Render's
/// home or an alternative that is unavailable for new endpoint admission.
pub(crate) fn relays_for_running_render(
    available_relays: &[px_node_protocol::IrohRelayConfig],
    retained_relays: &[px_node_protocol::IrohRelayConfig],
    description: Option<&px_node_protocol::IrohConnectionDescription>,
) -> Vec<px_node_protocol::IrohRelayConfig> {
    let mut relays = available_relays.to_vec();
    let Some(home_url) = description
        .and_then(|description| description.endpoint_address.get("addrs"))
        .and_then(serde_json::Value::as_array)
        .and_then(|routes| routes.iter().find_map(|route| route.get("Relay")))
        .and_then(serde_json::Value::as_str)
        .and_then(|relay_url| url::Url::parse(relay_url).ok())
    else {
        return relays;
    };
    let Some(retained_home) = retained_relays
        .iter()
        .find(|relay| url::Url::parse(&relay.url).is_ok_and(|relay_url| relay_url == home_url))
    else {
        return relays;
    };
    if let Some(available_home) = relays
        .iter_mut()
        .find(|relay| url::Url::parse(&relay.url).is_ok_and(|relay_url| relay_url == home_url))
    {
        // Console owns the current QAD configuration even when the URL is unchanged.
        *available_home = retained_home.clone();
    } else {
        if relays.len() == 32 {
            relays.pop();
        }
        relays.push(retained_home.clone());
    }
    relays
}

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

    fn relay(port: u16) -> px_node_protocol::IrohRelayConfig {
        px_node_protocol::IrohRelayConfig {
            url: format!("https://relay.example.test:{port}/"),
            qad_port: Some(port),
        }
    }

    fn description(home_url: &str) -> px_node_protocol::IrohConnectionDescription {
        px_node_protocol::IrohConnectionDescription {
            endpoint_address: serde_json::json!({"id": "a".repeat(64), "addrs": [{"Relay": home_url}]}),
            endpoint_configuration: serde_json::json!({}),
        }
    }

    #[test]
    fn full_or_draining_home_is_retained_only_by_its_current_render() {
        let available = vec![relay(4606)];
        let retained = vec![relay(4605), relay(4607)];
        assert_eq!(
            relays_for_running_render(&available, &retained, None),
            available
        );
        assert_eq!(
            relays_for_running_render(
                &available,
                &retained,
                Some(&description("https://relay.example.test:4605"))
            ),
            vec![relay(4606), relay(4605)]
        );
        assert_eq!(
            relays_for_running_render(
                &available,
                &retained,
                Some(&description("https://relay.example.test:4607/"))
            ),
            vec![relay(4606), relay(4607)]
        );
        assert_eq!(
            relays_for_running_render(
                &available,
                &[],
                Some(&description("https://relay.example.test:4605/"))
            ),
            available
        );
    }

    #[test]
    fn retained_home_uses_current_qad_without_duplicates_or_exceeding_candidate_limit() {
        let available = vec![relay(4605)];
        let mut retained_home = relay(4605);
        retained_home.qad_port = Some(4609);
        assert_eq!(
            relays_for_running_render(
                &available,
                &[retained_home.clone()],
                Some(&description("https://relay.example.test:4605"))
            ),
            vec![retained_home.clone()]
        );
        let available: Vec<_> = (4700..4732).map(relay).collect();
        let selected = relays_for_running_render(
            &available,
            &[retained_home.clone()],
            Some(&description("https://relay.example.test:4605/")),
        );
        assert_eq!(selected.len(), 32);
        assert_eq!(selected.last(), Some(&retained_home));
        assert_eq!(&selected[..31], &available[..31]);
    }

    #[test]
    fn ip_only_or_unregistered_home_does_not_add_retained_alternatives() {
        let available = vec![relay(4606)];
        let mut ip_description = description("https://relay.example.test:4605/");
        ip_description.endpoint_address["addrs"] = serde_json::json!([{"Ip": "192.168.1.10:4601"}]);
        assert_eq!(
            relays_for_running_render(&available, &[relay(4605)], Some(&ip_description)),
            available
        );
        assert_eq!(
            relays_for_running_render(
                &available,
                &[relay(4605)],
                Some(&description("https://unregistered.example.test:4605/"))
            ),
            available
        );
    }

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
