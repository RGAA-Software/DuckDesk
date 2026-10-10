# iroh Relay development deployment

`px_relay` now has an explicit iroh data plane on Windows and Linux, using pinned
iroh-relay 1.3.0. This is the first M1 implementation slice, not an installable
release. Console control/reporting is integrated; full Windows product installation
and runtime address refresh are still being completed. Android is scheduled last.

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
configuration is withheld until an eligible Relay exists. Node authentication or
reconnection refreshes this list; live updates to existing Render transports are
part of the subsequent multi-Relay work.

## Linux container package

`scripts_build/package_px_relay_linux.sh <fresh-binary> <new-directory> <pixels-relay:tag>`
builds a complete Relay image and saves it with Compose, examples and a SHA-256
manifest. Verify the package manifest, load `relay-image.tar`, and create
`config/relay.json`, `config/tls/` and `config/relay-control.env` beside Compose.
The container runs as UID/GID 10001; certificate key and configuration permissions
must permit that identity. Use `docker compose up -d --pull never`, then verify
the running image and `/opt/pixels/bin/px_relay` against the manifest.

The published host TCP/UDP ports default to 4605 and may be set in `.env`; match
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
Multi-Relay failover, full Windows packaging and final cross-host business
acceptance remain M1/M2 work; local or WSL results do not establish those outcomes.
