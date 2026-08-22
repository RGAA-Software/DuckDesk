# CEF dependency

WebView mode uses the pinned official Windows x64 CEF Standard Distribution in
`manifest.json`. Binary payloads are intentionally excluded from Git.

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File third_party/cef/fetch_cef.ps1
```

The script verifies the archive SHA-1 and extracts CEF beside this file. It uses
`HTTPS_PROXY`/`HTTP_PROXY` first and falls back to the proxy configured in Git.
Set `CEF_ROOT` to another extracted CEF directory if the build must use a shared
machine-level cache.

`build_official.bat` invokes this fetcher automatically before configuration;
after the checksum marker exists the call is local and does not download again.
The binary directory remains ignored by Git. `scripts/collect_dist.py` fails the
build if any required CEF runtime file or the `zh-CN` locale is missing.
