# Window

`window` is Vanguard's portable native-window and display-management core. It owns logical window identity, requested and native state, display topology, close transactions, and the multi-consumer event journal without depending on SDL, Win32, ImGui, Application, or a rendering API.

All mutations are explicit owner-thread operations. Window and display identities are generational, titles and backend state are copied into manager-owned storage, events use a bounded sequence journal with per-consumer cursors, and shutdown rejects live windows. Native backends implement `IWindowBackend`; they never become public engine identity.

The module deliberately contains no swapchain behavior. A later presentation layer will attach to a window through a generational `PresentationAttachmentHandle` and will acknowledge pixel/surface revisions after safely updating its swapchain.

At runtime, the engine `WindowService` owns `WindowManager` for the entire process service lifetime. The platform host supplies the backend and remains the sole native event pump. Closing the primary window requests application-state shutdown; the service explicitly accepts that close transaction during stop. Loading states, world transitions, and error states therefore retain the same responsive native window without owning its lifetime themselves.
