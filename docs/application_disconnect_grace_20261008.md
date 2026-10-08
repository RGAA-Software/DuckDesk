# Application disconnect grace

The Console application editor now owns `disconnect_grace_seconds` for Game Hook,
WebView and RDP applications. The default is 10 seconds; administrators can choose
an integer from 1 to 3600 seconds. English and Simplified Chinese labels and
validation share the same editor and support both Console themes.

Migration 0037 adds the bounded policy to applications and launch snapshots. The
runtime database role can update the application policy but cannot rewrite an
existing instance's snapshot. Existing application definitions receive 10 seconds.
Already running Render processes keep their original launch policy.

Reservation copies the policy into the instance, and the fenced node Start command
passes it through Service to Render as `--app_disconnect_grace_seconds`. Render
uses this duration only after the last connected client leaves. Reconnecting
invalidates the queued expiry; another connected client prevents expiry. The
never-connected startup timeout remains 45 seconds. RDP runtime exit preserves
the Windows account, profile, session and workspace applications.

Application edits also require node preparation to follow the current application
revision. Nodes now receive the current revision alongside its executable path;
a successfully fenced preparation receipt advances the deployment's observed
application revision. Old receipts remain rejected. This prevents an application
edit from leaving deployments permanently stale and repeatedly disconnecting the
node. Scheduling still requires matching application, deployment, node, endpoint
and control revisions. Running launches retain their captured configuration.

Render logs the effective policy at startup and the configured duration when the
last client disconnects. This change does not implement restarting an instance
after it has already exited.

Focused validation includes Render lifecycle tests, Service launch conversion for
all three modes, Console validation and persistence, stale preparation receipts,
immutable instance snapshots, node protocol integration, and browser configuration
in English/Chinese and light/dark themes. Runtime delivery uses complete Server and
Cloud Node Setup packages and installed payload SHA-256 verification.

## Validation on node 90

- Server 1.0.53: all 35 installed payload hashes matched; Console, Relay and Backup
  services were ready.
- Cloud Node 3.3.97 focused candidate `application-grace-verified-20261008`: all
  314 installed payload hashes matched; Service was ready. Development Render and
  Service artifacts were also published to their development dist with matching hashes.
- The browser verified all four existing application definitions migrated to 10
  seconds, then saved 20 seconds through the WebView editor in the live Console.
- The node automatically confirmed application revisions 4 and 5 without requiring
  a separate deployment edit.
- WebView stayed alive through a six-second disconnection. Reconnecting cancelled
  the old deadline, and the same instance remained running past that deadline.
- Changing the application back to 10 seconds did not shorten the running instance's
  20-second policy. Render logged final disconnect at 17:21:15.127 and idle exit at
  17:21:35.128 on 2026-10-08 (20.001 seconds).
- The next instance logged disconnect at 17:21:41.533 and idle exit at
  17:21:51.534 (10.001 seconds). Console reported normal `stopped` states and released
  capacity. Test instances and sessions were cleaned up; the setting was restored to 10.

Evidence is retained locally in `.cache/application-grace-live/measurements.json`,
`.cache/application-grace-live-final.log`, `.cache/application-grace-web-result.json`,
the corresponding browser screenshots, and `test-results/server_validation/`.
