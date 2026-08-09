# NVRHI build adapter

This Premake adapter builds the vendored NVRHI common layer and D3D12 backend as private static libraries. Engine code does not include NVRHI headers; only the Vanguard backend implementation links these projects.
