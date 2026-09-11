# Window

`WindowStateRequest::activateWhenShown` defaults to true. A visibility request with it set to false shows a window without requesting activation, for example when a tool creates a native host during an existing drag. The SDL adapter scopes the activation hint to that show operation; subsequent ordinary windows retain the previous effective setting.

`SetOpacity` is a synchronous owner-thread operation for whole-window translucency. It validates a live generational handle and a finite value in `[0, 1]`, then delegates to the backend; unsupported backends report failure. It does not request presentation-resource recreation or resizing, or alter surface revisions. The SDL backend uses `SDL_SetWindowOpacity`.

`window` is Vanguard's portable native-window and display-management core. It owns logical window identity, requested and native state, display topology, close transactions, and the multi-consumer event journal without depending on SDL, Win32, ImGui, Application, or a rendering API.

All mutations are explicit owner-thread operations. Window and display identities are generational, titles and backend state are copied into manager-owned storage, events use a bounded sequence journal with per-consumer cursors, and shutdown rejects live windows. Native backends implement `IWindowBackend`; they never become public engine identity.

Backend commands are synchronous and non-reentrant: a backend never calls the manager or delivers an event from inside `EnumerateDisplays`, `Create`, `ApplyWindowState`, `SetWindowTitle`, or `DestroyWindow`. The platform pump queues and delivers native events only after the command returns. Successful destruction is final and produces no later backend destruction event. The manager guards this contract so an accidental same-thread callback is rejected instead of deadlocking on the state lock.

The module deliberately contains no swapchain or rendering-API object. A renderer attaches its own presentation object to exactly one live, presentation-capable window through a generational `PresentationAttachmentHandle`. `PresentationAttachmentSnapshot` exposes the current pixel extent, display, mode, visibility, HDR capability, and the exact pixel/surface revisions still requiring render-safe work. The renderer acknowledges only revisions it has successfully applied; an older acknowledgement remains pending if the window changed concurrently, while future or regressive acknowledgements are rejected.

Presentation ownership is an explicit destruction barrier. The renderer must stop submitting work, wait for the relevant GPU completion point, release its back buffers/swapchain, and call `DetachPresentation` before the native window can be destroyed. The window manager never stores or destroys renderer pointers. Visibility/minimization and occlusion are exposed as independent requirements so a renderer may suspend acquisition or presentation without pretending it resized or recreated a surface.

`ResolvePresentationSurface` is the narrow native bridge used only by the presentation owner. The manager validates the
attachment and asks its backend for a platform-tagged, non-owning native surface. Backend window identities, SDL objects,
and rendering types remain private to their respective modules.

At runtime, the engine `WindowService` owns `WindowManager` for the entire process service lifetime. The platform host supplies the backend and remains the sole native event pump. Closing the primary window requests application-state shutdown; the service explicitly accepts that close transaction during stop. Loading states, world transitions, and error states therefore retain the same responsive native window without owning its lifetime themselves.
