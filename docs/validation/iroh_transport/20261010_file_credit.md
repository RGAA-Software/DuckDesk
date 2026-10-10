# File receive credit for mixed iroh traffic

## Change

The previous per-channel queue released bytes when a QUIC write completed locally. This did not bound bytes still buffered below the application. The preceding incompressible 64 MiB Relay run completed correctly but recorded 43 delivery gaps above 100 ms and a 429 ms maximum gap.

File traffic now reserves a 256 KiB cumulative receive window. The receiver issues a receipt after dispatching each file message to its business callback. Only that receipt releases credit. Two normal 120 KiB blocks can be outstanding; an existing legal message larger than the window, at most 1 MiB, is allowed alone. This bounds accepted file payload, not all TCP/QUIC buffers or every media byte. Receipts do not mean the destination file has been committed to disk; file completion and SHA checks remain separate.

Receipts use the existing control stream's priority. A zero-length control record is followed by an eight-byte big-endian cumulative file payload count. They never reach business message callbacks. Partial receipts survive receive timeouts, and invalid/regressing receipts close the session. Channel framing is PXQ version 2; both Windows endpoints must be updated together. Android's existing data paths remain active and unchanged. No new authorization or transport token is introduced.

Queued callbacks retain smart ownership. Credit waiters close on stop; a waiter may stop the session from its callback. File queues do not consume input/control credit or block datagram admission. This change addresses bulk backlog, not TCP head-of-line blocking after packet loss.

Pre-change source contents, including existing local modifications, are preserved under `backup/iroh_file_receipts_20261010/` with an archive manifest. The iroh dependency patch is unchanged.

## Focused verification

- Release CTest: `iroh_file_flow`, `iroh_transport`, `iroh_frontend` passed: 24 cases passed, three environment-dependent Relay cases skipped. Existing public candidate/recovery evidence is reused; skipped cases are not counted as passed.
- New tests cover withheld receipts after local write/remote read, input and datagram progress during file backpressure, bidirectional receipts, invalid/duplicate counts, partial framing across timeouts, callback-triggered stop, and repeated stop with pending credit.
- Client build entry points also run their existing focused Client tests and publish runtime artifacts with hash verification.

## Deployment and real traffic

Complete candidate `iroh-file-credit-20261010`, Cloud Node 3.3.97, was installed on 90 through its Setup under `D:\112233`. All 314 installed artifacts match the manifest; three services are Running. Installed Render SHA-256 is `952F3EFE0794FDCBCB3ECE660C048AE815174CF5525ED94BD065269FE0DDF84A`. Setup, manifest, installed identity and development build/dist hashes are in `20261010_file_credit_{build,installation}.json`. No persistent configuration change was introduced.

Each row below transfers 64 MiB of incompressible block contents in each direction using the actual Client while dragging the actual game view. Every upload/download SHA matches. FPS and gaps are transport frame-delivery observations, not display latency or input round-trip measurements. Timing includes the harness workflow; these are successive real-network runs, not a controlled identical loss trace.

| Case | Workflow seconds | Delivery FPS | Maximum gap ms | Gaps >100 ms | Maximum sampled RTT ms |
|---|---:|---:|---:|---:|---:|
| Before, direct | 29.239 | 46.8–60.3 | 289.8 | 5 | 304.943 |
| After, direct | 25.711 | 57.4–59.0 | 199.1 | 1 | 15.289 |
| Before, 90 Relay | 44.951 | 20.2–59.6 | 429.0 | 43 | 435.955 |
| After, 90 Relay | 47.413 | 53.9–60.4 | 290.0 | 13 | 115.950 |
| After, 90 Relay repeat | 38.640 | 59.0–60.4 | 90.7 | 0 | 62.013 |

Both endpoints report advancing file receipts and bounded outstanding payload; the Client's periodic post-receipt samples peak at 122,993 bytes. These are samples, not proof that every instantaneous value equals that number; the unit/real-channel tests verify the window boundary. Upload and download both experience backpressure and resume via writable signals.

The direct run's single 199.1 ms gap includes a 163.3 ms source-frame gap. The first Relay after-run still has 13 short gaps; the worst 290 ms event has an 83.3 ms source gap and a 10-microsecond assembly call. Thus the change is effective against bulk backlog, but does not establish complete elimination of source or transport jitter. No new packet capture was taken this batch; the preceding dual capture's TCP-HOL finding remains valid, without attributing every new gap to a captured retransmission.

## Credit recovery and cleanup

With a live file transfer using 90 Relay, the Relay service was actually stopped and restored (3.275 seconds from stopped observation to restored observation). The same Client had a 3.856-second media gap, then resumed video and completed both file directions with matching SHA; one business admission, no replacement Client/session. The total workflow was 38.579 seconds. The last measured delivery window recovered to 58.3 FPS, maximum 53 ms. This is a short service-interruption recovery check for the new credit mechanism, not a long outage or general reliability claim. Remote/local wall clocks differ, so only durations within each clock are compared.

Evidence: `20261010_file_credit_summary.json`, direct/Relay/repeat/outage JSON and logs, `20261010_file_credit_outage_injection.json`, and `20261010_file_credit_render_cleanup.log`. Owned Clients, instances, guest sessions and remote test files were cleaned up; no active resource sessions remain. Only desktop Render PID 24392 remains; 90 and BJ are fresh/ready and not draining. The Relay service is restored, and no machine trace, firewall or persistent policy was changed.

## Next unfinished work

The file credit change is delivered and its focused functional/recovery checks pass. M3 media performance is still open: retain the first after-run's residual gaps and distinguish source-frame pauses, Relay TCP retransmission waits and any remaining queue delay before tuning further. Reuse these completed gates. Missing NAT/long-term stability evidence remains explicit; Windows/Linux delivery precedes Android and old-data-path retirement.
