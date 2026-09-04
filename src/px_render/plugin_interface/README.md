# Legacy WebRTC compatibility ABI

This directory is not the Render extension API. It contains the frozen ABI
used only by `net_rtc.dll`, `net_rtc_local.dll`, and the private side of
`webrtc_library_host.cpp`, plus transitional owned event values still consumed
by the executable.

Executable-owned capture, encoder, and network modules must derive from
`RenderModule`, never `PxPluginInterface` or `PxNetPlugin`. New extensibility is
defined by `architecture/extensions/flow_node_plugin.h` and is organized by
Source, Processor, Encoder, Observer, and Sink roles.

Do not add a new feature-specific base class, `GetInstance` export, directory
scanner, service locator, or generic event route here. The existing WebRTC
factory identity and unload timing are frozen compatibility behavior.
