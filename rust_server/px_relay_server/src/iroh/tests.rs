use std::{
    net::{TcpListener, UdpSocket},
    time::Duration,
};

use tokio_util::sync::CancellationToken;

use super::{IrohRelay, IrohRelayConfig};

fn configuration(directory: &std::path::Path) -> IrohRelayConfig {
    let certificate = rcgen::generate_simple_self_signed(vec!["localhost".into()]).unwrap();
    let config = IrohRelayConfig {
        https_bind: TcpListener::bind("127.0.0.1:0")
            .unwrap()
            .local_addr()
            .unwrap(),
        qad_bind: UdpSocket::bind("127.0.0.1:0")
            .unwrap()
            .local_addr()
            .unwrap(),
        certificate_file: directory.join("relay.pem"),
        private_key_file: directory.join("relay-key.pem"),
        max_connections: 2,
        qad_public_port: None,
        console_managed: false,
    };
    std::fs::write(&config.certificate_file, certificate.cert.pem()).unwrap();
    std::fs::write(
        &config.private_key_file,
        certificate.signing_key.serialize_pem(),
    )
    .unwrap();
    config
}

#[tokio::test]
async fn cancellation_releases_both_listeners_for_repeated_start() {
    let directory = tempfile::tempdir().unwrap();
    let config = configuration(directory.path());
    for _cycle in 0..3 {
        let relay = IrohRelay::start(&config).await.unwrap();
        assert!(TcpListener::bind(config.https_bind).is_err());
        assert!(UdpSocket::bind(config.qad_bind).is_err());
        let cancellation = CancellationToken::new();
        let task = tokio::spawn(relay.run(cancellation.clone()));
        cancellation.cancel();
        tokio::time::timeout(Duration::from_secs(12), task)
            .await
            .unwrap()
            .unwrap()
            .unwrap();
        assert!(TcpListener::bind(config.https_bind).is_ok());
        assert!(UdpSocket::bind(config.qad_bind).is_ok());
    }
}

#[tokio::test]
async fn occupied_qad_port_does_not_leak_https_listener() {
    let directory = tempfile::tempdir().unwrap();
    let config = configuration(directory.path());
    let occupied = UdpSocket::bind(config.qad_bind).unwrap();
    assert!(IrohRelay::start(&config).await.is_err());
    // Upstream aborts partially started tasks when construction fails.
    tokio::time::timeout(Duration::from_secs(2), async {
        loop {
            if TcpListener::bind(config.https_bind).is_ok() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    })
    .await
    .unwrap();
    drop(occupied);
}

#[test]
fn certificate_paths_resolve_against_configuration_directory() {
    let directory = tempfile::tempdir().unwrap();
    let config_path = directory.path().join("relay.json");
    std::fs::write(&config_path, r#"{"https_bind":"127.0.0.1:18460","qad_bind":"127.0.0.1:18460","certificate_file":"relay.pem","private_key_file":"relay-key.pem"}"#).unwrap();
    let config = IrohRelayConfig::load(&config_path).unwrap();
    assert_eq!(
        config.certificate_file,
        std::fs::canonicalize(directory.path())
            .unwrap()
            .join("relay.pem")
    );
    std::fs::write(&config_path, r#"{"https_bind":"127.0.0.1:18460","qad_bind":"127.0.0.1:18460","certificate_file":"","private_key_file":"relay-key.pem"}"#).unwrap();
    assert!(IrohRelayConfig::load(&config_path).is_err());
}
