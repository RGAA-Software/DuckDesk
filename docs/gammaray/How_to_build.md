### How to build
#### 1. Clone the repo
```c++
    git clone --recursive https://github.com/RGAA-Software/GammaRay.git
```

#### 2. Install dependencies by vcpkg
This project uses an external vcpkg instance. The default root is configured in `env_premium.cmake` (`C:/source/vcpkg`), and the triplet is `x64-windows-static-release`.

- 2.1 Change to the vcpkg folder
```c++
    cd C:/source/vcpkg
```
- 2.2 Install vcpkg if it is not bootstrapped yet
```c++
    .\bootstrap-vcpkg.bat
```
- 2.3 Install dependencies (replace the triplet if you use a different one)
```c++
    .\vcpkg.exe install gflags:x64-windows-static-release
    .\vcpkg.exe install sqlite3:x64-windows-static-release
    .\vcpkg.exe install detours:x64-windows-static-release
    .\vcpkg.exe install gtest:x64-windows-static-release
    .\vcpkg.exe install libvpx:x64-windows-static-release
    .\vcpkg.exe install opus:x64-windows-static-release
    .\vcpkg.exe install fftw3:x64-windows-static-release
    .\vcpkg.exe install easyhook:x64-windows-static-release
    .\vcpkg.exe install glm:x64-windows-static-release
    .\vcpkg.exe install sdl2:x64-windows-static-release
    .\vcpkg.exe install jemalloc:x64-windows-static-release
	.\vcpkg.exe install cpr:x64-windows-static-release
	.\vcpkg.exe install mongo-cxx-driver:x64-windows-static-release
	.\vcpkg.exe install drogon:x64-windows-static-release
    .\vcpkg.exe install breakpad:x64-windows-static-release
    .\vcpkg.exe install mimalloc:x64-windows-static-release
    .\vcpkg.exe install protobuf:x64-windows-static-release
```

- 2.4 You can open the project by Visual Studio 2022 or Clion, solve the problems and then compile the project.
