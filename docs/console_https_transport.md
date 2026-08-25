# Console HTTPS/WSS transport

## Scope

Console is the only component that exposes a TLS listener. Its TCP port serves
both HTTPS APIs and WSS upgrades. Native clients accept the deployment's
self-signed certificate without certificate-chain or host-name verification.

| Connection | Transport |
| --- | --- |
| Browser / Console Web -> Console | HTTPS and WSS |
| Panel -> Console | HTTPS and WSS |
| Windows Client -> Console | WSS |
| Windows Service -> Console | HTTPS and WSS |
| Panel / Windows Client -> Render | Existing HTTP, WS, WebRTC or UDP |
| Render local IPC and user proxy | Existing loopback HTTP / WS |
| UDP Direct media | Existing plain UDP |

Render-hosted WebClient pages continue to use the Render's HTTP origin. A link
returned by Console navigates the browser to that top-level Render page; it is
not embedded as mixed content in the Console HTTPS page.

## Runtime policy

- `px_console.toml` uses `ssl_enable = true`.
- A legacy `ssl_enable = false` value is migrated to `true` in memory at startup.
- Console has no plain HTTP listener or automatic TLS downgrade.
- `ssl_cert` and `ssl_key` are resolved relative to the Console executable
  directory unless an absolute path is configured.
- The bundled self-signed certificate is sufficient. Browsers may require the
  user to accept the certificate warning once for the exact Console origin.
- Panel migrates a stored `console_ssl_enable=false` value to `true`.
- Legacy client arguments and access-info fields are still parsed for wire and
  command-line compatibility, but cannot disable Console TLS.

## Acceptance tests

1. Start Console with the bundled certificate and confirm TCP 30500 accepts
   `https://` and rejects plain `http://`.
2. Open the Console Web origin, accept the browser warning, and confirm its WSS
   `/console/website` connection remains established across heartbeat cycles.
3. From a Panel containing the old `console_ssl_enable=false` preference,
   restart Panel and confirm the setting migrates to true.
4. Register, log in, log out and create a guest session from Panel over HTTPS.
5. Confirm Panel `/console/panel`, Windows Client `/console/client`, and Windows
   Service `/console/service` use WSS and reconnect after Console restarts.
6. Confirm the advertised access info contains `srv_ssl_enable=true`.
7. Confirm a missing or malformed certificate prevents Console from listening
   instead of falling back to HTTP.
8. Confirm Render TCP 20371 remains HTTP/WS and UDP Direct remains UDP.
9. Launch Render-hosted WebClient from a Console-issued URL and confirm RTC
   signaling, video, audio and input are unchanged.
10. Repeat HTTPS/WSS checks from another LAN machine using the Console IP and
    the browser's explicit self-signed-certificate exception.

Connection authentication must also pass this matrix. A logged-in Panel uses a
Console-issued one-time ticket; a logged-out Panel keeps the password-bearing
shared-link/direct-connect path. Neither client implementation may silently
substitute one path for the other.

| Panel identity | Windows Client | Render WebClient |
| --- | --- | --- |
| Logged in | Ticket + nonce; no remote password | Ticket in URL fragment + nonce |
| Logged out | Shared-link password/direct authentication | Shared-link password/direct authentication |

For every cell require a real RTC connection and a decoded video frame. Web
acceptance additionally requires audio receivers and an open input channel;
native acceptance additionally requires the file transport to connect.

## Validation record (2026-08-24, build 3.3.56)

- The official Web, C++, Rust client and Rust server builds completed. Build
  artifacts and their `build_official/dist` or `output/px_console` copies were
  verified with matching SHA-256 hashes.
- Local Console returned HTTP 200 for HTTPS `/ping`, the SPA and guest REST
  requests. Plain HTTP failed, while `/console/website` returned a WebSocket
  `101 Switching Protocols` response over TLS.
- Chrome loaded the Console SPA over HTTPS with the explicit self-signed
  certificate exception.
- Guest, register, login and logout REST calls all returned code 200. The
  temporary acceptance user and sessions were removed from MongoDB afterward.
- On the LAN test host, both `console_ssl_enable=false` and the legacy
  `cms_ssl_enable=false` were injected before restarting the current Panel and
  Service. Both reconnected through their canonical WSS routes. They also
  reconnected after a live Console restart.
- The native Windows Client was launched with the legacy
  `--console_ssl=false` argument. It stayed on canonical `/console/client`, did
  not fall back to `/cms/client`, decoded the first remote video frame and did
  not report a declined WebSocket handshake.
- The Render boundary remained unchanged: its WebClient loaded over HTTP on
  TCP 20371, an HTTPS handshake on that port failed, and Render still owned the
  UDP 20371 endpoint.

## Authentication matrix regression (2026-08-24)

- Fixed stale local device recovery after Console data is reset. The device
  query endpoint returns HTTP 400 with business code 602 (`DeviceNotFound`);
  Panel now reads the business code, recreates the device record and then
  resumes ticket issuance. Other HTTP 400 responses are not treated as a
  missing device.
- The 10.0.0.90 target passed all four real connection cells. Logged-in and
  logged-out WebClient sessions each received 1920x1080 video, two audio
  receivers and sent input on an open data channel. Logged-in and logged-out
  Windows Client sessions each connected RTC, decoded the first key frame,
  initialized audio playback and connected the file transport.
- The originally failing local device also passed a logged-in native
  ticket/nonce self-connection after automatic record recovery. Temporary test
  users, sessions and tickets were removed after each run. Its newly generated
  password/direct WebClient link also passed video, audio and input, proving
  that recovery updates both the ticket path and the logged-out path.
