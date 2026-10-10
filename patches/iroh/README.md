# iroh 1.3.0: disconnected Relay send queues

User approved this minimal isolated-source exception on 2026-10-10. The original Cargo registry cache and reference checkouts remain read-only.

`rust_transport/vendor/iroh-1.3.0` is extracted from the published crate, preserving licenses and all 84 files. Only
`src/socket/transports/relay/actor.rs` differs. `manifest.json` records the crate SHA-256, upstream revision, patch identity,
original/modified file hashes and all isolated file hashes. Cargo resolves this copy through the transport workspace patch table.

`0001-drain-disconnected-relay-queues.patch` drains undeliverable datagrams during disconnected dialing and reconnect backoff.
The original periodic drain restarts with every dial; repeated failures faster than its three-second interval leave the queue full.
The shared Relay actor can then wait on that queue and block packets addressed to a healthy Relay. QUIC handles retransmission of
reliable payload; this patch changes neither healthy Relay writes nor application authentication.

The earlier `*-draft.patch` and `draft-base.json` are review history, not build inputs. Do not apply both patches.

Validation commands:

```powershell
python scripts/tests/verify_iroh_vendor.py
cargo test --manifest-path rust_transport/Cargo.toml --release --locked -p px_transport -p px_transport_ffi
python scripts/tests/run_iroh_dual_relay_business.py --certificate-dir .cache/iroh-private-qad --server-auto --output test-results/iroh-queue.json
```

The last command requires a focused `test_iroh_frontend` build and the private TLS test fixture. It continuously sends video before
and during failure, holds the selected Relay offline, then requires fresh-address business readmission and received video. Idle-only
tests did not reproduce this defect. See `docs/validation/iroh_transport/status.md` for installation and public-network results.
