/*
 * Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
 */
#pragma once

class CCubeTexture;

namespace tools
{
	class BACKEND_ENGINE_API EnvironmentPresetsManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		struct SPreviewPreset
		{
			SPreviewPreset();

			void Load();

			red::String				m_name;
			Vector4					m_lightColor;
			Vector3					m_lightDir;
			red::String				m_cubemapName;
			THandle< CCubeTexture >	m_cubemap;	// env cubemap
			Float					m_fixedToneMappingLuminance;
		};

		EnvironmentPresetsManager();
		~EnvironmentPresetsManager();

		RED_INLINE Uint32 GetPresetsCount() const { return m_presets.Size(); }
		RED_INLINE const red::DynArray< String >& GetPresetNames() const { return m_presetsNames; }
		RED_INLINE Bool PresetExists( const String& name ) const { return m_presetsNames.Exist( name ); }

		Bool GetEnvModePreset( const red::String& presetName, SPreviewPreset& outPreset );

	private:

		void InitializePreviewPresets();
		void ClearPreviewPresets();

		red::DynArray< SPreviewPreset > m_presets;
		red::DynArray< red::String > m_presetsNames;
	};
}