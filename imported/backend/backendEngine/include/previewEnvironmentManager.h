#pragma once

#include "objectCollection.h"
#include "environmentPresetsManager.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/resourceMonitor.h"
#include "../../../common/worldEnvironment/include/environmentUtils.h"
#include "../../../common/world/include/runtimeSystemEnvironment.h"

class CCubeTexture;

namespace world { class EnvironmentDefinition; class EnvironmentAreaParameters; class RuntimeScene; }

namespace tools
{
	enum SubstanceParityMode
	{
		SP_Disabled, 		// default
		SP_Linear,
		SP_ACES,
	};

	class BACKEND_ENGINE_API EnvironmentManager : public ISerializable
	{
		RTTI_DECLARE_TYPE( EnvironmentManager );

	public:
		EnvironmentManager();
		EnvironmentManager( const red::SharedPtr< EnvironmentPresetsManager >& presetsManager );
		~EnvironmentManager();

		// Set light rotation (Yaw angle)
		void SetTimeOfDay( const Float timeOfTheDay );

		// Get current light rotation (Yaw angle)
		Float GetTimeOfTheDay() const;

		// Set env probe rotation (Yaw angle)
		void SetEnvProbeYawAngle( const Float lightRotation );

		// Get current env probe rotation (Yaw angle)
		Float GetEnvProbeYawAngle() const;

		// Get current env probe rotation matrix
		const Quaternion& GetEnvProbeRotation() const;

		// Check if there's any env mode selected
		Bool IsEnvPreviewPresetSelected() const;

		// Get current environment mode preset
		void GetCurrentEnvPreviewPreset( EnvironmentPresetsManager::SPreviewPreset& currentPreset ) const;

		// Set env mode
		void SetEnvPreviewPreset( const red::String& mode );

		// Get current env mode
		void GetEnvPreviewPreset( red::String& mode ) const;
		
		// Is substance mode toggled on
		SubstanceParityMode GetSubstanceMode() const { return m_substanceMode; }

		const red::SharedPtr< EnvironmentPresetsManager >& GetPresetsManager() const;

		// Fill frame info with environment data
		void PerformOverrides( CRenderFrameInfo& info );

		// Toggle substance mode
		Bool SetSubstanceMode( SubstanceParityMode mode );

		typedef std::function< void( const String& ) > TOnEnvModeChanged;
		TOnEnvModeChanged OnEnvModeChanged;

		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr<rtti::ValueHolder>& outValue ) const override;
		virtual Bool WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr<rtti::ValueHolder>& newValue ) override;

		void SetRuntimeScenePtr( const red::SharedPtr< world::RuntimeScene >& ptr );

	private:
		Float						m_timeOfDay;			// time of the day (0.0f - 24.0f)
		SubstanceParityMode			m_substanceMode;		// substance parity mode
		Float						m_envProbeYaw;			// light probe rotation (yaw)
		Quaternion					m_envProbeRotation;		// light probe rotation
		Bool						m_showEnvProbeAsSkybox; // show env probe as skybox

		EnvironmentPresetsManager::SPreviewPreset m_envPreviewPreset;
		EEnvManagerModifier			m_envModifier;			// Not an actual value used by rendering; Gets synchronised with RuntimeSystemEnvironment::GetDisplayMode() every frame

		// Environment settings
		THandle< world::EnvironmentAreaParameters>		m_tonemappingParameters;
		THandle< world::EnvironmentAreaParameters>		m_envPreviewParameters;

		IEditActionHistory*							m_actionHistory;

		// interop
		//interop::InteropNotifier*	m_envProbeRotationNotifier{};
		//interop::InteropNotifier*	m_substanceModeNotifier{};
		//interop::InteropNotifier*	m_timeOfDayNotifier{};

		red::SharedPtr< EnvironmentPresetsManager >	m_presetsManager;

		red::WeakPtr< world::RuntimeScene > m_runtimeScene; //For RuntimeSystemEnvironment access

		static const char sm_envBasePath[];
		static const char sm_substanceParityParametersPath[];
		static const char sm_substanceACESParityParametersPath[];

		void UpdateEnvProbeRotation();
		Bool LoadAndSetEnvironmentParameters( const char envPath[], THandle<world::EnvironmentAreaParameters>& param);

		// Set env definition
		void SetEnvParameters( const THandle< world::EnvironmentAreaParameters >& newValue, THandle<world::EnvironmentAreaParameters>& param );
		void ClearEnvParameters( THandle<world::EnvironmentAreaParameters>& param );
	};
}


RTTI_DECLARE_ENUM_IN_NAMESPACE( SubstanceParityMode, tools );