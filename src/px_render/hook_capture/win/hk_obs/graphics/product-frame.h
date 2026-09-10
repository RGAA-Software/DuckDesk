#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OBS C adapter: the caller retains the texture through this synchronous call.
struct ID3D11Texture2D;
bool px_graphics_ready(void);
bool px_publish_shared_frame(struct ID3D11Texture2D* texture); // NOLINT(gammaray-raw-pointer-boundary) Borrowed OBS C ABI.

#ifdef __cplusplus
}
#endif
