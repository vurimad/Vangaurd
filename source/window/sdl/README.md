# SDL Window Backend

`windowSdl` is the cross-platform native implementation of Vanguard's portable `IWindowBackend`. SDL owns OS window creation and native event translation, while `WindowManager` remains the authority for engine identities, requested state, close decisions, topology revisions, and event history.

The process platform host is the only SDL event pump. Each polled event is routed to input, this backend, and later editor integrations in its original order. `ProcessEvent` translates window events and independently marks display-topology events so `WindowService` can refresh displays before accepting a window transition onto a newly added or changed display.

Windows are created hidden, configured completely, and only then shown. The backend supports parent/modal relationships, constraints, high-density windows, windowed and fullscreen modes, Vulkan-capable creation, state queries, HDR/display metadata, and explicit destruction. It owns no swapchain and exposes no SDL identity as Vanguard's public window identity.

Display fingerprints use stable backend identity and the native display name; mutable topology such as bounds, desktop resolution, and refresh rate is deliberately excluded so ordinary display reconfiguration remains an update rather than a false physical replacement.
