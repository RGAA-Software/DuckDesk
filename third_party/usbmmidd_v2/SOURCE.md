# USBMMIDD v2 source and verification

This directory contains the unmodified `usbmmidd_v2` package used by RustDesk's
Windows release workflow.

- Upstream vendor: Amyuni Technologies Inc.
- RustDesk release asset: https://github.com/rustdesk-org/rdev/releases/download/usbmmidd_v2/usbmmidd_v2.zip
- Downloaded: 2026-08-21
- Archive SHA-256: `629B51E9944762BAE73948171C65D09A79595CF4C771A82EBC003FBBA5B24F51`
- Driver version from `usbmmIdd.inf`: `2.0.0.1` (2021-08-31)

The driver catalog and both UMDF driver DLLs have valid Microsoft Windows
Hardware Compatibility Publisher signatures. The two `deviceinstaller`
utilities are not Authenticode-signed; GammaRay verifies their pinned hashes
before invoking the x64 utility.

The original license is retained as `License.txt`. Product documentation and
redistributions must preserve the Amyuni attribution required by that license.
