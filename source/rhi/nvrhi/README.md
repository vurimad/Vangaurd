# Vanguard D3D12 backend

This module privately maps Vanguard's GpuApi-shaped RHI contract to NVRHI's D3D12 implementation. Public engine and renderer modules must depend on `rhi`, never NVRHI. Native and NVRHI types are confined to this module's source files.

The D3D12 backend uses one narrow private adapter to obtain the underlying `ID3D12Heap` from the pinned NVRHI D3D12 implementation. This is required because placed resources are resident and evicted at heap granularity. Textures and buffers continue through NVRHI's standard `D3D12_Resource` native-object path, and no native object enters Vanguard's public contract.

The common backend owns API-independent resource construction, upload and transfer command generation, texture copy/resolve/readback validation, fence-stamped staging ownership and mapping, command-list recording, state transitions, clear/discard validation, dynamic output state, GPU event ranges, multi-queue submission and resource retirement. The D3D12 layer owns adapter/device/queue creation, device-loss reporting, query heaps, raw clock calibration, rectangular clear and discard callbacks not exposed by NVRHI, native debug names, presentation and the native queue fences used by the common timeline. This separation is the contract future Vulkan support must follow.
