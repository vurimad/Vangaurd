/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace InGameConfig
{
	template< typename T >
	void Var::InternalSetValue( const T newValue, const Source source )
	{
		m_sourceOfChange = source;

		if ( source == Source::Settings || source == Source::PresetSettings )
		{
			RED_ASSERT( !IsDisabled(), "Changing value should not be possible by interacting with UI when the variable is disabled" );
			switch ( m_updatePolicy )
			{
			case VarUpdatePolicy::Disabled:
				RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Engine, "Trying to change value for in-game config var that has update policy set to disabled" );
				break;

			case VarUpdatePolicy::Immediately:
				InternalSetRequestedValue( &newValue );
				ScheduleVarUpdateWithoutConfirmation();
				break;

			case VarUpdatePolicy::ConfirmationRequired:
				InternalSetRequestedValue( &newValue );
				ScheduleVarUpdateWithConfirmation();
				break;

			case VarUpdatePolicy::RestartRequired:
				InternalSetRequestedValue( &newValue );
				ScheduleVarUpdateWithRestart();
				break;

			case VarUpdatePolicy::LoadLastCheckpointRequired:
				InternalSetRequestedValue( &newValue );
				ScheduleVarUpdateWithLoadLastCheckpoint();
				break;

			default:
				ALWAYSENABLED_RED_FATAL( "Unknown in-game config var update policy (%d)", static_cast<int>( m_updatePolicy ) );
				break;
			}
		}
		else if ( source == Source::Code || source == Source::PresetCode )
		{
			InternalSetRequestedValue( &newValue );
			ScheduleVarUpdateWithoutConfirmation();
		}
		else if ( source == Source::LoadSettings )
		{
			InternalSetRequestedValue( &newValue );
			ScheduleVarLoadFromSettings();
		}
		else
		{
			RED_FATAL( "Invalid source of change" );
		}
	}
}