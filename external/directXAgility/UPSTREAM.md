# Upstream

- Package: `Microsoft.Direct3D.D3D12`
- Version: `1.619.6`
- Agility SDK integer: `619`
- Source: `https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.619.6`
- Package SHA-256: `08F0489281401AA430FC37322D6C3FC98A8025175AACD714C10D562F4963F1E9`
- `D3D12Core.dll` SHA-256: `37FA14281A58CC834076971873006FEB8A8D25CDDC908D1A345BDA1B149FFC7D`
- `d3d12SDKLayers.dll` SHA-256: `7AB48D56D1C18708EBCF16C7C9259B2D783E2B935B85ADD4A244573B9F03CAB4`
- Integration: the x64 redistributable runtime is stored under `1.619.6/bin/x64` and deployed app-locally for Vanguard executables that instantiate the D3D12 backend.
- License: Microsoft DirectX redistributable terms; see `1.619.6/LICENSE.txt` and `1.619.6/distributable files.txt`.
- Header/runtime policy: Vanguard builds against the separately pinned DirectX-Headers import (`external/directXHeaders`, currently `v1.717.0-preview`) while shipping the stable Agility runtime `1.619.6`. Runtime-facing D3D12 features must therefore remain capability-gated and must not assume preview-header APIs are present in SDK 619.

Only files explicitly listed by Microsoft as redistributable are vendored here. Vanguard does not download or resolve the Agility SDK at build or runtime.
