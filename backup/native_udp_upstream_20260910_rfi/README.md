# Output-confirmed RFI archive

Base: `5d90f4de288d0cd85166c0f75646ec3d9c43b873`. Batch `native_udp_upstream_20260910_rfi`.
Full pre-edit working files preserved before replacing fixed invalidation checks and adding confirmed encoder output metadata.
Reference only; excluded from all build/runtime discovery. Existing RTC/WS event consumers retain their original behavior.

| Path | State | SHA-256 |
|---|---|---|
| src/px_render/architecture/encoders/nvenc/nvenc_video_encoder.cpp | clean | 0869E4A9921F62A04CEABCEC31EBEE7E373B44A2A49B6B9C01F228333FF94EBD |
| src/px_render/architecture/encoders/nvenc/nvenc_video_encoder.h | clean | 108A7F71D61887311FF6D39FC7948217A94FB67DA30E7F1AFA982F5B0039A232 |
| src/px_render/architecture/events/render_event.h | modified | 7E27D9FEFBDB929F92001243CD93CE4F9C68890F4E3B967FC8C9F3A1AB7D63AF |
| src/px_render/modules/render_module_registry.cpp | modified | 70F791953FB62CFA59FB7798FE93A984221C3913C481FC4A749665028D7AAD96 |
