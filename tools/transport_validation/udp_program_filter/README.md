# Isolated Windows transport UDP restriction

This acceptance helper applies UDP block conditions only to the exact isolated
`unified-acceptance-20261013/T4-tools/px_nat_probe.exe` (or the standard
`px_transport_probe.exe`) path. It does not
enable Windows Firewall profiles or change product transport configuration.
Both IPv4 and IPv6 connect/receive layers match the application identity and
UDP protocol. TCP and other executable paths remain outside these conditions.

Build with `cargo build --release --jobs 18`. Do not run it on the local
development machine during remote acceptance. Run as an administrator on the
designated test peer with arguments: probe path, new JSON-lines report path,
maximum duration (60–900 seconds). Create the report's `.stop` companion to
request cleanup. The helper closes its dynamic session, queries each owned
filter ID and reports whether any remain. The hard deadline also closes it.

WFP dynamic sessions automatically remove their objects when the session ends:
[Microsoft FwpmEngineOpen0](https://learn.microsoft.com/en-us/windows/win32/api/fwpmu/nf-fwpmu-fwpmengineopen0).
Application/protocol filter conditions follow Microsoft's
[WFP sample](https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/wfp/msnfilter/msnfilter.cpp).

Filter creation alone is not acceptance evidence. Verify an ordinary direct
connection first, demonstrate UDP-only connection failure while the filters
are active, then verify automatic Relay selection with `relay_only=false` and
the standard probe's reliable-stream and datagram checks. Confirm filter
removal and direct connectivity restoration afterward. Preserve failed samples.
