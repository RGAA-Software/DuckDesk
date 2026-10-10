# iroh Relay development deployment

`px_relay` now has an explicit iroh data plane on Windows and Linux, using pinned
iroh-relay 1.3.0. Windows Server Setup and Linux Compose installation, Console
control/reporting, live Relay candidate updates and two-host recovery have existing
validation evidence. See `docs/validation/iroh_transport/status.md` for installed
identities and remaining performance/NAT/stability work. Android is scheduled last.

## Build and start

Windows: run `scripts_build\build_px_relay_server.bat`. Linux: run
`bash scripts_build/build_px_relay_server.sh`. Both use Release and verify the
copied development binary's SHA-256. `PIXELS_RELAY_TARGET_DIR` overrides the build
directory; Linux also accepts `PIXELS_RELAY_OUTPUT_DIR`.

Copy `iroh-relay.example.json` into your deployment configuration directory and
provide a PEM certificate chain and matching private key. Relative certificate
paths are resolved against that JSON file, independent of service working directory.
The certificate must cover the actual hostname/IP used by clients. Private CAs
must be distributed through the existing trust configuration; TLS verification is
not disabled.

```powershell
.\output\px_relay\dev\px_relay.exe --iroh-config C:\Pixels\Relay\relay.json
```

```sh
./output/px_relay/dev-linux/px_relay --iroh-config /etc/pixels/relay/relay.json
```

Open the configured HTTPS TCP port and QAD UDP port. They may share a port number
because they use different transports. 4605 is an example for a dedicated Relay:
do not bind it over an existing Relay instance. Each additional host runs its own
configuration and certificate. Clients must be given the advertised HTTPS URL
and QAD port; wildcard bind addresses are never client addresses.

## Service lifecycle

The existing Windows `Pixels.Relay` service entry (`--service <absolute-env-file>`)
accepts `PIXELS_RELAY_IROH_CONFIG=C:\Pixels\Relay\relay.json` in its environment
file. Linux systemd uses the same variable with a Linux absolute path in the
existing `pixels-private-relay@.service` EnvironmentFile. Container processes
receive it through their environment or use `--iroh-config` directly.

Windows SCM stop, Ctrl+C, and Linux SIGTERM use the cancellation/shutdown path.
The Relay stops accepting new clients and bounds listener shutdown to ten seconds.
Existing full product Setup/Compose workflows remain the installation mechanism;
these focused binaries are for development process checks, not replacement packages.

## Current scope

- HTTPS Relay forwarding and QUIC address discovery; payload authorization remains
  the existing login/resource session flow at Render. No new transport tickets.
- Configurable concurrent Relay connections (not application instance count),
  exact connection-release accounting, and internal draining control.
- Logs report actual active Relay connections and forwarded/received payload bytes.
- An explicitly selected invalid iroh configuration fails startup. It does not
  switch to the previous protocol. The previous entry remains for consumers that
  have not migrated, until Android is completed and final archival is safe.

## Console management

The example enables `console_managed`. Register the Relay's public hostname/IP
and HTTPS port in Console, then supply its existing registration credential using:

```text
PIXELS_RELAY_CONSOLE_CONTROL_URL=wss://console.example:4600/api/console/relay-control
PIXELS_RELAY_CONSOLE_CA_FILE=/etc/pixels/relay/tls/console-ca.pem
PIXELS_RELAY_NODE_TOKEN=<existing registration credential>
```

The CA file is optional when Console uses a publicly trusted certificate.
Do not populate retired room-protocol app/control keys for iroh. `qad_public_port`
overrides the reported UDP port when NAT maps it to a different external port;
otherwise the listening QAD port is reported.

Console registration starts in maintenance/draining state. Release maintenance
in the Relay management page once configured. Control disconnection blocks new
admissions; authenticated reconnect restores Console's desired state. Reports
contain actual connections and payload bytes. iroh room counts are null and the
UI shows “Not applicable”. Console migration 0039 must precede the updated Relay.

`PIXELS_CONSOLE_IROH_CONFIGURATION` now contains management-wide policy only:
`{"relay_only":false,"ca_certificates_pem":[]}`. Static `relays` entries are
rejected: addresses come from fresh, enabled, non-draining registered iroh Relays
with connection capacity. Include private Relay CA PEM certificates in the policy.
Forced Relay mode can be configured before any Relay is ready; node connection
configuration is withheld until an eligible Relay exists. Node reports and Service
heartbeats update existing Render candidates; Clients refresh candidates through
their existing resource sessions. Candidate updates preserve healthy active
connections. They do not promise instant migration of existing traffic.

## Linux container package

`scripts_build/package_px_relay_linux.sh <fresh-binary> <new-directory> <pixels-relay:tag>`
builds a complete Relay image and saves it with Compose, the deployment script,
examples and a SHA-256 manifest. This is separate from the focused binary build.
On the Linux host, prepare a persistent directory such as `/etc/pixels/relay`
containing `relay.json`, `tls/` and `relay-control.env`. Keep this directory across
upgrades. The container runs as UID/GID 10001; certificate key and configuration
permissions must permit that identity. The JSON must bind HTTPS and QAD on
`0.0.0.0:4605` (or `[::]:4605`) inside the container.

Run from the newly built complete package:

```sh
python3 deploy.py --verify-only
python3 deploy.py --config-directory /etc/pixels/relay --project-name pixels-relay
```

The deployment script verifies all packaged files before Docker changes, loads
the image, verifies its identity and runtime SHA, then replaces only the `relay`
service using Compose. It checks the resulting container and runtime SHA. It never
rewrites the configuration or runs `compose down`. Python 3.8+, Docker Engine and
the Docker Compose plugin are required.

For upgrades, supply **the existing project's name**, even when the new package
is in another directory. Read it from the existing container if necessary:

```sh
docker inspect --format '{{index .Config.Labels "com.docker.compose.project"}}' <existing-container>
```

Changing the project name creates a separate service and may conflict with the
existing listener. Keep the same external configuration and host ports. Set
`--https-port` and `--qad-port` when host ports differ from 4605. An interrupted
upgrade can be retried with the same arguments; to revert the image, use a retained
complete package with this deployment entry point and the same project/configuration.
No automatic rollback or configuration downgrade is performed.

The script reports container/runtime identity, **not Console readiness**. After
deployment, check `docker logs --tail 50 <container_id>` using the returned container
ID, then confirm fresh/ready status in Console and one focused actual Relay connection.
Failed Docker commands report their operation without dumping configuration or
credentials; inspect Docker/service logs on the host for the detailed failure.

Windows upgrades continue to use the complete Server Setup. Existing Relay JSON,
TLS and registration configuration stays under the private configuration root;
the installer grants the service read access, including shared Console TLS files.
Do not replace installed Windows executables with focused development outputs.

The published host TCP/UDP ports default to 4605; match
Console's public HTTPS port and the JSON `qad_public_port`. Private keys and
registration credentials stay in the mounted configuration, outside the image.
Both host firewall and cloud security group must permit the configured ports.

To check UDP independently of HTTPS Relay forwarding, build the Release
`px_qad_probe` binary from `rust_transport/px_transport_probe` and run:

```text
px_qad_probe <public-ip:udp-port> <certificate-hostname-or-ip> <trusted-ca.pem>
```

It verifies TLS, bounds the connection/address-discovery wait to ten seconds,
and prints the observed public mapping. Its elapsed time includes connection
shutdown; it is not a media latency benchmark. A successful forced Relay test
alone does not verify this UDP address-discovery endpoint.

For an explicitly isolated transport check, `console_managed:false` runs without
Console management. It is not a fallback from failed managed authentication.
Two-host Relay recovery, live candidates, maintenance and connection capacity have
recorded real-host evidence. Remaining NAT, long-duration stability and throughput
distribution checks are tracked separately; local/WSL packaging checks do not
replace those outcomes or establish Android acceptance.
