# P0 CMake integration snapshot

Base revision: `5d90f4de288d0cd85166c0f75646ec3d9c43b873`.

Original path: `src/px_client_sdk/CMakeLists.txt`.
The file had no pre-existing working-tree changes when copied.
SHA-256 of both original and archive before integration:
`13F56861DA118BACE30874D0AFF9E10074F5CFD6C4C00672B0E7ACBC4F03529D`.

Reason: retain the complete pre-change CMake entry before adding the independent
media core/reference test subdirectory. No production transport is retired by
this batch. This directory is reference-only and is not compiled or packaged.
