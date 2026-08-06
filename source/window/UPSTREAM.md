# Upstream provenance

RED separates viewport-window ownership from engine viewport creation, publishes explicit resize, movement, activation, and close notifications, and retains requested viewport properties until the renderer applies them at a safe boundary. Vanguard keeps those proven contracts while removing raw listener ownership, native handles as viewport identity, global renderer access, and renderer-owned native-window lifetime.

Vanguard additionally separates logical, pixel, and safe-area geometry; uses signed virtual-desktop positions; binds close decisions to serial numbers; exposes display topology through generational handles; and provides a bounded multi-consumer event journal instead of mutable listener arrays.
