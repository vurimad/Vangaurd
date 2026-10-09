#ifndef VG_D3D12_AGILITY_SDK_VERSION
#error VG_D3D12_AGILITY_SDK_VERSION must be defined for a D3D12 Agility host.
#endif

static_assert(sizeof(unsigned int) == 4);

extern "C"
{
    // The Windows D3D12 loader reads these exports before Vanguard initializes
    // its RHI. Keep them in the final executable rather than a static library so
    // link-time dead stripping cannot discard the Agility SDK request.
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = VG_D3D12_AGILITY_SDK_VERSION;
    __declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
}
