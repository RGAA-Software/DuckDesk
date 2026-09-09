# Reference-only archive: dedicated Web text WebSocket

Base revision: `e40329672354007282104cc85ad1b5943ed02b6b`.
Batch: `application_text_websocket_retirement_20260909_2345`.
Reason: user requires reuse of the existing reliable control connection instead
of an additional WebSocket. Web reuses its existing reliable ordered media
RTCDataChannel (ordinary input remains a separate unreliable channel).

All files were copied intact from the actual working tree immediately before
retiring WebSocket-specific branches. This includes uncommitted implementations,
not reconstructed Git HEAD content.

| Original path | Local status before archive |
|---|---|
| `web/px_web_client/src/App.vue` | Modified tracked file |
| `web/px_web_client/src/rtc/application_text.ts` | Untracked new implementation |
| `web/px_web_client/test/application_text.test.ts` | Untracked new tests |
| `web/px_web_client/vite.config.ts` | Modified tracked file |
| `web/px_web_client/APPLICATION_TEXT.md` | Untracked implementation notes |

Reference only. Excluded from compilation, test discovery, packaging and runtime;
not an executable fallback or a second maintained transport implementation.
