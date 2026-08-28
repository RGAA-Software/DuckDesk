# Parsec VDD provenance

## Controller source

- Upstream: https://github.com/nomi-san/parsec-vdd
- Commit: `a827c7137659b618d0a65f261ad8b2da1c74f772`
- Describe: `v0.45.1-119-ga827c71`
- Vendored path: `source/`
- License: `source/LICENSE`

The controller source is pinned to the upstream commit above. GammaRay's
maintained product fork adds a headless `-worker` mode, changes heartbeat to
50 ms, uses a product-specific single-instance identity, retains the upstream
eight-display capacity, allows five seconds for Windows topology removal, and
overrides the MSBuild `AssemblyName` property at build time to produce
`px_display.exe`.

## Driver package

- Official package: https://builds.parsec.app/vdd/parsec-vdd-0.45.0.0.exe
- Package version: `0.45.0.0`
- Package SHA-256: `E23332448FDAF5AA017CB308DB5EF6855FAC526A7DED05D80C039404126D5362`
- Hardware ID: `Root\Parsec\VDA`

The driver files were extracted without modification. `mm.cat` and `mm.dll`
have valid signatures issued to `Microsoft Windows Hardware Compatibility
Publisher` by `Microsoft Windows Third Party Component CA 2014`.

Do not modify `driver/mm.cat`, `driver/mm.dll`, or `driver/mm.inf`; changing the
signed package invalidates its catalog signature.
