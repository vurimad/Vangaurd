# Design provenance

The public device and resource-reference behavior follows the explicit GpuApi boundary used by RED. NVRHI supplies the private native backend implementation. Vanguard owns validation, typed generation references, lifetime policy, diagnostics, render-graph contracts and submission policy.

Vanguard's private D3D12 backend includes the pinned NVRHI implementation header to obtain the native heap used by residency priority, make-resident and eviction operations on placed allocations. The vendor checkout is not modified and this dependency does not cross the backend boundary.
