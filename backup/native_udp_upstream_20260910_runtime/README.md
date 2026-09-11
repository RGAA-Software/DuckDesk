# Native UDP production replacement archive

Base revision: `5d90f4de288d0cd85166c0f75646ec3d9c43b873`.
Batch: `native_udp_upstream_20260910_runtime`. Full working files copied before removing legacy media branches; reference only, never built or packaged.
Reason: replace legacy video/audio packetization, assembly and adaptive FEC with the pinned upstream media core. Control/association/voice remain active.

| Original path | Working state | SHA-256 |
|---|---|---|
| src/px_client_sdk/connection/udp_direct_connection.h | clean | 0B9FD3C6805153D2C424A6F3D47FAAD63A77F8AE206C8E43EBA4F1359AB00131 |
| src/px_client_sdk/connection/udp_direct_connection.cpp | modified | 33C076270C4B42EEC03F4A886934A7AF32764C13A2F42E77D183A99422F2E7BC |
| src/px_render/network/udp/udp_transport.h | clean | CE4D8A4E5B98072AE2D630516F6E159B56EEA2A6460A10655F9EE530AA7DED07 |
| src/px_render/network/udp/udp_transport.cpp | modified | C2B14C559054BCECE856451641C1CD178ABE473AD821CFFE05EBD1F0900DF402 |
| src/px_render/network/udp/CMakeLists.txt | clean | BB15B1525BB6945FA52C30FA1EDB47D4E429BF63942796DC7509E1E20143FBE4 |
| src/px_client_sdk/CMakeLists.txt | modified | AD374DE2BBF2C0823804DBE879A0BE82EB0E71EE04A382AE0D9A58AC5A356690 |
| src/px_deps/px_common/px_udp_protocol.h | modified | 48FD23ECEC6ED3F89EC51152F75AE9605F4E03E17AF7FF59F1AF15FAC8601BEE |
