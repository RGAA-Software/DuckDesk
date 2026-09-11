# Game display power request

Base revision: `5d90f4de288d0cd85166c0f75646ec3d9c43b873`.
Full pre-change working copies retain their repository-relative paths.
`owned_game_process.h/.cpp` were unchanged from HEAD; `tests/CMakeLists.txt`
already contained uncommitted work and is preserved intact.

Reason: keep the interactive display active while a product-owned game exists,
release requests on stop/failure, and wake an already idle display before launch.
No machine power-plan edits, background compatibility implementation, or build inclusion.
