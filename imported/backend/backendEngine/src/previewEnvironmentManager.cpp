#include "build.h"
#include "previewEnvironmentManager.h"
#include "editActionHistory.h"

#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/worldRuntimeScene.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( SubstanceParityMode, tools );
{
	RTTI_ENUM_OPTION( SP_Disabled );
	RTTI_ENUM_OPTION( SP_Linear );
	RTTI_ENUM_OPTION( SP_ACES );
}
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( EnvironmentManager, tools );
{
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_envModifier ).editable();
	RTTI_PROPERTY( m_envProbeYaw ).editable().range( 0, 360 );
}
RTTI_END_TYPE();

using red::String;

namespace tools
{
	const char EnvironmentManager::sm_envBasePath[] = "engine\\preview_panel\\preview_panel_env_base.envparam";
	const char EnvironmentManager::sm_substanceParityParametersPath[] = "engine\\preview_panel\\substance_parity_weather_params.envparam";
	const char EnvironmentManager::sm_substanceACESParityParametersPath[] = "engine\\preview_panel\\substance_parity_weather_params_aces.envparam";

	class EnvMode_CustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( EnvMode_CustomProperty );

	public:
		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override
		{
			static const CName handledType = RED_NAME_CONSTEXPR( "envModePreset" );
			return customTypeName == handledType;
		}
		
		virtual CName GetControlType() const override
		{
			return RED_NAME( String::Printf( "%hs:Restricted", ::GetTypeName<String>().AsChar() ) );
		}
		
		virtual Bool HasOptions() const override
		{
			return true;
		}
		
		virtual const red::DynArray<String>& GetOptions() const override
		{
			if ( THandle< EnvironmentManager > envManager = FindParentObject< EnvironmentManager >() )
			{
				return envManager->GetPresetsManager()->GetPresetNames();
			}
			return IEditableCustomProperty::GetOptions();
		}
	};

	EnvironmentManager::EnvironmentManager()
	{}

	EnvironmentManager::EnvironmentManager( const red::SharedPtr< EnvironmentPresetsManager >& presetsManager)
		: m_timeOfDay( 12.0f )
		, m_substanceMode( SubstanceParityMode::SP_Disabled )
		, m_envProbeYaw( 0.f )
		, m_showEnvProbeAsSkybox( true )
		, m_presetsManager( presetsManager )
		, m_envModifier( EEnvManagerModifier::EMM_None )
	{

		UpdateEnvProbeRotation();

		m_actionHistory = RED_NEW(IEditActionHistory)(nullptr);

		LoadAndSetEnvironmentParameters(sm_envBasePath, m_envPreviewParameters );
		SetEnvPreviewPreset( "none" );
	}

	EnvironmentManager::~EnvironmentManager()
	{
		RED_DELETE( m_actionHistory );
	}

	void EnvironmentManager::SetTimeOfDay( const Float timeOfTheDay )
	{
		m_timeOfDay = timeOfTheDay;
	}

	Float EnvironmentManager::GetTimeOfTheDay() const
	{
		return m_timeOfDay;
	}

	void EnvironmentManager::SetEnvProbeYawAngle( const Float envProbeRotation )
	{
		m_envProbeYaw = envProbeRotation;
		UpdateEnvProbeRotation();
	}

	Float EnvironmentManager::GetEnvProbeYawAngle() const
	{
		return m_envProbeYaw;
	}

	const Quaternion& EnvironmentManager::GetEnvProbeRotation() const
	{
		return m_envProbeRotation;
	}

	Bool EnvironmentManager::IsEnvPreviewPresetSelected() const
	{
		return !m_envPreviewPreset.m_name.Empty() && m_envPreviewPreset.m_name != "none";
	}

	void EnvironmentManager::GetCurrentEnvPreviewPreset(EnvironmentPresetsManager::SPreviewPreset& currentPreset) const
	{
		currentPreset = m_envPreviewPreset;
	}

	void EnvironmentManager::SetEnvPreviewPreset( const String& mode )
	{
		if ( mode != m_envPreviewPreset.m_name)
		{
			m_presetsManager->GetEnvModePreset( mode, m_envPreviewPreset );
			if ( OnEnvModeChanged )
			{
				OnEnvModeChanged( mode );
			}
		}
	}

	void EnvironmentManager::SetEnvParameters(const THandle< world::EnvironmentAreaParameters >& newValue, THandle<world::EnvironmentAreaParameters>& param )
	{
		RED_ASSERT( newValue, "Valid environment definition must be provided." );

		param = newValue;
	}

	void EnvironmentManager::ClearEnvParameters( THandle<world::EnvironmentAreaParameters>& param )
	{
		param = nullptr;
	}

	void EnvironmentManager::GetEnvPreviewPreset(String& envMode) const
	{
		envMode = m_envPreviewPreset.m_name;
	}

	const red::SharedPtr< EnvironmentPresetsManager >& EnvironmentManager::GetPresetsManager() const
	{
		return m_presetsManager;
	}

	void EnvironmentManager::UpdateEnvProbeRotation()
	{
		m_envProbeRotation = EulerAngles( 0.0f, 0.0f, m_envProbeYaw - 90.0f ).ToQuat();
	}

	void EnvironmentManager::PerformOverrides( CRenderFrameInfo& info )
	{
		// Apply env preview overrides
		if( IsEnvPreviewPresetSelected())
		{
			EnvironmentPresetsManager::SPreviewPreset preset;
			GetCurrentEnvPreviewPreset( preset );

			DayPointEnvironmentParams& envDayPointParams = info.m_envParametersDayPoint;
			auto& baseLightingParams = info.m_baseLightingParameters;

			// Setup override cubemap
			envDayPointParams.m_forcedEnvProbeDataSource = preset.m_cubemap ? preset.m_cubemap->GetRenderResource() : nullptr;
			envDayPointParams.m_forcedEnvProbeRotation	 = GetEnvProbeRotation().ToMatrix();

			// Setup light direction from HDR metadata
			baseLightingParams.m_lightDirection = preset.m_lightDir;
			baseLightingParams.m_lightColor		= preset.m_lightColor;
			baseLightingParams.m_sunColor		= preset.m_lightColor;

		}
		
		// Apply tonemapping environment overrides
		if (m_tonemappingParameters)
		{
			world::envUtils::OverrideEnvironmentInfo( info, m_tonemappingParameters );
		}

		//Hacky way of detecting if the RuntimeSystemEnvironment's value was modified by some external tool (ImGui based debug tool for instance)
		if ( const red::SharedPtr< world::RuntimeScene > runtimeScene = m_runtimeScene.Lock() )
		{
			if ( auto* systemEnvironment = runtimeScene->GetSystem< world::RuntimeSystemEnvironment >() )
			{
				const EEnvManagerModifier currentDisplayMode = systemEnvironment->GetDisplayMode();
				if ( currentDisplayMode != m_envModifier )
				{
					m_envModifier = currentDisplayMode;
					NotifyPropertyChanged( rtti::AccessPath( "envModifier" ) );
				}
			}
		}
		else
		{
			//We're not in a world/prefab editor, there's no runtime scene -> keep the ownership of EMMs
			info.m_displaySettings.m_displayMode = m_envModifier;
		}
	}

	Bool EnvironmentManager::SetSubstanceMode(SubstanceParityMode mode)
	{
		// Set environment definition compliant with substance visual parity mode
		Bool result = true;

		if ( mode != m_substanceMode )
		{
			m_substanceMode = mode;
			if (mode != SubstanceParityMode::SP_Disabled)
			{
				result = LoadAndSetEnvironmentParameters( mode == SubstanceParityMode::SP_Linear ? sm_substanceParityParametersPath : sm_substanceACESParityParametersPath, m_tonemappingParameters );
			}
			else
			{
				ClearEnvParameters(m_tonemappingParameters);
			}
		}
		return result;
	}

	void EnvironmentManager::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, newValue );

		if ( path.ToString().EqualsNC( "envProbeYaw" ) )
		{
			UpdateEnvProbeRotation();
		}
		else if ( path.ToString().EqualsNC( "envModifier" ) )
		{
			if ( const red::SharedPtr< world::RuntimeScene > runtimeScene = m_runtimeScene.Lock() )
			{
				if ( auto* systemEnvironment = runtimeScene->GetSystem< world::RuntimeSystemEnvironment >() )
				{
					systemEnvironment->SetDisplayMode( m_envModifier );
				}
			}
		}
	}

	void EnvironmentManager::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		rtti::ClassEditablePropertyInfo propInfo;
		propInfo.m_name = RED_NAME_CONSTEXPR( "substanceParityMode" );
		propInfo.m_type = GetTypeObject< Uint32 >();
		outRootProperties.Add( propInfo );

		propInfo.m_name = RED_NAME_CONSTEXPR( "envMode" );
		propInfo.m_type = GetTypeObject< String >();
		propInfo.m_customTypeName = RED_NAME_CONSTEXPR( "envModePreset" );
		outRootProperties.Add( propInfo );
	}

	Bool EnvironmentManager::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "envMode" ) )
		{
			outValue = rtti::ValueHolder::CreateSingle( m_envPreviewPreset.m_name.AsChar() );
			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "substanceParityMode" ) )
		{
			return GetTypeObject< Uint32 >()->ReadValue( ctx, &m_substanceMode, restOfThePath, outValue );
		}
		return ISerializable::ReadCustomEditableProperty( ctx, propertyName, restOfThePath, outValue );
	}

	Bool EnvironmentManager::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValueHolder )
	{
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "envMode" ) )
		{
			SetEnvPreviewPreset( newValueHolder->GetSingleValue() );
			return true;
		}
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "substanceParityMode" ) )
		{
			SubstanceParityMode newValue;
			if ( GetTypeObject< Uint32 >()->WriteValue( ctx, &newValue, restOfThePath, *newValueHolder.Get() ) )
			{
				return SetSubstanceMode( newValue );
			}
			return false;
		}
		return ISerializable::WriteCustomEditableProperty( ctx, propertyName, restOfThePath, newValueHolder );
	}

	void EnvironmentManager::SetRuntimeScenePtr( const red::SharedPtr< world::RuntimeScene >& ptr )
	{
		m_runtimeScene = red::WeakPtr< world::RuntimeScene >( ptr );
	}

	Bool EnvironmentManager::LoadAndSetEnvironmentParameters( const char envPath[], THandle<world::EnvironmentAreaParameters>& param )
	{
		// Load environment
		auto token = GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build(envPath));
		token->WaitUntilLoaded();
		if (token->IsLoaded())
		{
			if (token->GetResource())
			{
				SetEnvParameters( Cast<world::EnvironmentAreaParameters>( token->GetResource() ), param );
				return true;
			}
		}

		RED_LOG_ERROR( "'%hs' resource couldn't be found in engine\\preview_panel directory. Please make sure it's there.", envPath );
		return false;
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( EnvMode_CustomProperty, tools )
	RTTI_PARENT_TYPE( tools::IEditableCustomProperty );
RTTI_END_TYPE();