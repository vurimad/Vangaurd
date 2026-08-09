# Vanguard D3D12 backend

This module privately maps Vanguard's GpuApi-shaped RHI contract to NVRHI's D3D12 implementation. Public engine and renderer modules must depend on `rhi`, never NVRHI. Native and NVRHI types are confined to this module's source files.

The common backend owns API-independent resource construction, upload command generation, command-list recording, state transitions, multi-queue submission and resource retirement. The D3D12 layer owns adapter/device/queue creation, device-loss reporting and the native queue fences used by the common timeline. This separation is the contract future Vulkan support must follow.
