/*
Official miniaudio translation unit (v0.11.25).
https://github.com/mackron/miniaudio

Optional feature cuts keep the plugin binary smaller; loopback capture only
needs the low-level device API.
*/
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
