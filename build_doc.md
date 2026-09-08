### Windows

#### 1. Open CMD
!!! MUST USE THIS !!!  
x64 Native Tools Command Prompt for VS 2022

#### 2. Build

Build entry points now live in `scripts_build\`; run them from the repository root
or by absolute path. Shared helper scripts remain in `scripts\`.

For routine development, use `scripts_build\build_cpp_client.bat`,
`scripts_build\build_cpp_render.bat`, or another focused entry point described in
[the incremental build guide](docs/cpp_incremental_build.md).

##### 2.1 OpenSource
This checkout does not provide a `build_opensource.bat` entry point.

##### 2.2 Official
> scripts_build\build_official.bat

Release/full builds only; this command also builds Web/Rust components and bumps the version.

#### 3. Local validation delivery rule

The Windows client is launched and validated from `build_official\dist`. A successful
compile alone is not a completed local delivery: synchronize all changed runtime
artifacts into `build_official\dist` and verify the source and dist SHA-256 hashes
match before asking for validation.
