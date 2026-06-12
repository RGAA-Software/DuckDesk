`src/GammaRay/src/service` is now the active integration surface for service-related C++ code.

Current contents:

- `service_manager.cpp`
- `service_manager.h`
- `CMakeLists.txt`

The Windows service executables are no longer built from C++ here.

- Rust `rust_client/gr_service` builds `GammaRayService.exe`
- Rust `rust_client/gr_service/service_manager` builds `GammaRayServiceManager.exe`

Historical C++ service sources were moved to [`legacy`](./legacy/) for reference only and are not part of the active build.
