# Retired dedicated application-text WebSocket implementation

Base revision: e40329672. All files below were locally modified; full actual working-tree contents were preserved, not reconstructed from HEAD.

User decision: Qt and Web must reuse existing reliable connections and only add protocol messages. Retire the additional WebSocket route and auxiliary channel events. This batch is reference-only and excluded from build/test/package/runtime discovery.

Original paths (preserved relative to this batch):

- src/px_render/network/ws/ws_server.cpp
- src/px_render/network/ws/ws_server.h
- src/px_render/architecture/events/render_event.h
- src/px_render/ingress/network_event_ingress.cpp
- src/px_render/ingress/network_event_ingress.h
- src/px_render/ingress/render_event_ingress.cpp

These are snapshots of still-maintained files before removing only the retired branches. Active media routes, protocol handling and text-input service remain maintained outside this archive. Source/archive SHA-256 equality was checked at copy time.
