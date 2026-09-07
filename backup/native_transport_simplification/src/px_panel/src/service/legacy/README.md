This directory contains the retired C++ implementation of the GammaRay Windows service layer.

Status:

- not built by current CMake
- kept only for historical analysis and migration reference
- replaced in production by:
  - `rust_client/px_service` -> `px_service.exe`
  - `rust_client/px_service/service_manager` -> `px_service_manager.exe`

The only active C++ code left in the parent `service` directory is the wrapper
`service_manager.cpp`, which delegates to the Rust manager binary.
