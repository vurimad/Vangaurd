#include "build.h"
#include "environmentPresetsManager.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/resource/include/2darray.h"
#include "../../../common/redReflection/include/resourceUtils.h"

#pragma warning(disable:4996) // deprecated functions

res::TStaticResource< C2dArray > g_PreviewPanelPresets( RED_CONST_RESOURCEPATH( "engine\\preview_panel\\preview_panel_presets.csv" ), GRF_Backend | GRF_Startup );

namespace tools
{

	EnvironmentPresetsManager::SPreviewPreset::SPreviewPreset()
		: m_lightColor( 0, 0, 0, 1 )
		, m_lightDir ( 0, 0, 0 )
		, m_fixedToneMappingLuminance( -1 )
	{
	}

	void EnvironmentPresetsManager::SPreviewPreset::Load()
	{
		if ( !m_cubemap && !m_cubemapName.Empty() )
		{
			THandle< CCubeTexture > cubemap = LoadResource_DEPRECATED<CCubeTexture>( m_cubemapName );
			if ( cubemap )
			{
				cubemap->GetRenderResource();
				m_cubemap = cubemap;
			}
		}
	}

	EnvironmentPresetsManager::EnvironmentPresetsManager()
		: m_presets{ red::PoolBackend() }
		, m_presetsNames{ red::PoolBackend() }
	{
		InitializePreviewPresets();
	}

	EnvironmentPresetsManager::~EnvironmentPresetsManager()
	{
		ClearPreviewPresets();
	}

	Bool EnvironmentPresetsManager::GetEnvModePreset( const red::String& presetName, SPreviewPreset& outPreset )
	{
		InitializePreviewPresets();

		for ( Uint32 i = 0; i < m_presets.Size(); ++i )
		{
			if ( m_presets[i].m_name == presetName )
			{
				m_presets[i].Load();
				outPreset = m_presets[i];
				return true;
			}
		}
		return false;
	}

	void EnvironmentPresetsManager::InitializePreviewPresets()
	{
		if ( !m_presets.Empty() )
		{
			return;
		}

		THandle< C2dArray > presetsArray = g_PreviewPanelPresets.Get();
		RED_ASSERT( presetsArray, "Expected preview presets" );
		if ( presetsArray )
		{
			for ( Uint32 row_i = 0; row_i < presetsArray->GetNumberOfRows(); ++row_i )
			{
				const String presetName = presetsArray->GetValue( ( "name" ), row_i );
				const String presetCubemapName = presetsArray->GetValue( ( "cube" ), row_i );
				const String presetLightDirString = presetsArray->GetValue( ("lightDir"), row_i );
				const String presetSunColorString = presetsArray->GetValue( ( "sunColor" ), row_i );
				const String presetSunIntensityString = presetsArray->GetValue( ( "sunIntensity" ), row_i );
				const String presetFixedToneMappingLuminanceString = presetsArray->GetValue( ( "fixedToneMappingLuminance" ), row_i );
				if ( presetName.Empty() || presetCubemapName.Empty() || presetLightDirString.Empty() || presetSunColorString.Empty() || presetSunIntensityString.Empty() || presetFixedToneMappingLuminanceString.Empty() )
				{
					continue;
				}

				Vector3 presetLightDir( 1, 0, 1 );
				if ( 3 != red::SScanf( presetLightDirString.AsChar(), ( "%f %f %f" ), &presetLightDir.X, &presetLightDir.Y, &presetLightDir.Z ) )
				{
					continue;
				}

				Vector4 presetSunColor( 0, 0, 0 );
				if ( 3 != red::SScanf( presetSunColorString.AsChar(), ( "%f %f %f" ), &presetSunColor.X, &presetSunColor.Y, &presetSunColor.Z ) )
				{
					continue;
				}

				Float presetSunIntensity = 0;
				if ( 1 != red::SScanf( presetSunIntensityString.AsChar(), ( "%f" ), &presetSunIntensity ) )
				{
					continue;
				}

				Float presetFixedToneMappingLuminance = 0;
				if ( 1 != red::SScanf( presetFixedToneMappingLuminanceString.AsChar(), ( "%f" ), &presetFixedToneMappingLuminance ) )
				{
					continue;
				}

				SPreviewPreset preset;
				preset.m_name					= presetName;
				preset.m_lightColor				= Vector4( 1.f, 1.f, 1.f, 0.f ) * presetSunColor * presetSunIntensity;
				preset.m_lightDir				= presetLightDir.Normalized();
				preset.m_cubemapName			= presetCubemapName;
				preset.m_fixedToneMappingLuminance = presetFixedToneMappingLuminance;

				m_presetsNames.PushBack( presetName );
				m_presets.PushBack( preset );
			}
		}
	}

	void EnvironmentPresetsManager::ClearPreviewPresets()
	{
		m_presets.Clear();
		m_presetsNames.Clear();
	}

}