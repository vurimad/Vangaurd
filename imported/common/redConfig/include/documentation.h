/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

// There are two callbacks called when variable changes its value:
// - Handler - it is uniquely registered for a group and handles variables changes in that group
//     Get current value from variable using GetValue()/GetIndex()
//     Get new value from variable using GetRequestedValue()/GetRequestedIndex()
//     For given group name there could be only one handler in the system for that group
//     Returns true if variable was handled/changed
// - Listener - could be many, registered for a group, notifies that variable in that group was changed
//     Get changed value from variable using GetValue()/GetIndex()

// Given this example options.json:
//
// [
// 	{
// 		"group_name": "/example/settings",
// 		"options" : [
// 		{
// 			"name": "VarBool",
// 			"display_name" : "1. VarBool",
// 			"description" : "1. VarBool",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "bool",
// 			"value" : true,
// 			"default_value" : true
// 		},
// 		{
// 			"name": "VarInt",
// 			"display_name" : "2. VarInt",
// 			"description" : "2. VarInt",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "int",
// 			"value" : 1,
// 			"default_value" : 1,
// 			"min_value" : 0,
// 			"max_value" : 100,
// 			"step_value" : 10
// 		},
// 		{
// 			"name": "VarFloat",
// 			"display_name" : "3. VarFloat",
// 			"description" : "3. VarFloat",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "float",
// 			"value" : 1.0,
// 			"default_value" : 1.0,
// 			"min_value" : 0.0,
// 			"max_value" : 10.0,
// 			"step_value" : 0.5
// 		},
// 		{
// 			"name": "VarIntList",
// 			"display_name" : "4. VarIntList",
// 			"description" : "4. VarIntList",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "int_list",
// 			"index" : 2,
// 			"default_index" : 0,
// 			"values" : [
// 				1,
// 				2,
// 				3,
// 				4,
// 				5
// 			]
// 		},
// 		{
// 			"name": "VarFloatList",
// 			"display_name" : "5. VarFloatList",
// 			"description" : "5. VarFloatList",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "float_list",
// 			"index" : 2,
// 			"default_index" : 0,
// 			"values" : [
// 				1.0,
// 				2.0,
// 				3.0,
// 				4.0,
// 				5.0
// 			]
// 		},
// 		{
// 			"name": "VarStringList",
// 			"display_name" : "6. VarStringList",
// 			"description" : "6. VarStringList",
// 			"is_visible" : true,
// 			"is_pre_game_only" : false,
// 			"update_policy" : "immediately",
// 			"type" : "string_list",
// 			"index" : 1,
// 			"default_index" : 2,
// 			"values" : [
// 				"asdfg",
// 				"qwerty",
// 				"zxcvb"
// 			]
// 		}
//	}
// ]
//
// You can handle this /example/settings group from your code:
//
// static CName s_settingsGroup( RED_NAME_CONSTEXPR( "/example/settings" ) );
//
// class SomeSystemNeedingSettings
// {
// 	void Initialize()
// 	{
// 		auto &config = InGameConfig::System::GetInstance();
//
// 		const auto handler = [ this ]( CName groupPath, CName varName, InGameConfig::VarType varType )
// 		{
// 			return HandleVarChange( groupPath, varName, varType );
// 		};
//
// 		m_groupHandlerID = config.RegisterHandler( s_settingsGroup, handler );
//
// 		const auto listener = [ this ]( CName groupPath, CName varName, InGameConfig::VarType varType )
// 		{
// 			OnVarChanged( groupPath, varName, varType );
// 		};
//
// 		m_varListenerID = config.RegisterListener( s_settingsGroup, listener );
// 	}
//
// 	void Uninitialize()
// 	{
// 		InGameConfig::System::GetInstance().UnregisterHandler( m_groupHandlerID );
// 		InGameConfig::System::GetInstance().UnregisterListener( m_varListenerID );
// 	}
//
// 	Bool HandleVarChange( CName groupPath, CName varName, InGameConfig::VarType varType )
// 	{
// 		DebugDumpVar( "SET", groupPath, varName, varType );
//
// 		if ( groupPath == s_settingsGroup )
// 		{
// 			return HandleSettingsVarChange( varName, varType );
// 		}
//
//
// 		return false;
// 	}
//
// 	Bool HandleSettingsVarChange( CName varName, InGameConfig::VarType varType )
// 	{
// 		const InGameConfig::System &system = InGameConfig::System::GetInstance();
// 		const InGameConfig::Group &group = system.GetGroup( s_settingsGroup );
//
// 		if ( varName == RED_NAME_CONSTEXPR_NOREG( "VarBool" ) )
// 		{
// 			m_myBool = group.GetVarBool( RED_NAME_CONSTEXPR( "VarBool" ) ).GetRequestedValue();
// 			return true;
// 		}
// 		else if ( RED_NAME_CONSTEXPR( "VarInt" ) )
// 		{
// 			// Do something with it
// 			return true;
// 		}
//
// 		return false;
// 	}
//
// 	void OnVarChanged( CName groupPath, CName varName, InGameConfig::VarType varType )
// 	{
// 		DebugDumpVar( "GET", groupPath, varName, varType );
// 	}
//
//
// private:
// 	InGameConfig::GroupHandler::ID m_groupHandlerID{};
// 	InGameConfig::VarListener::ID m_varListenerID{};
// 	Bool m_myBool{};
// };

// To dynamically add a variable or change it's values (for example display resolution):
//
// auto &config = InGameConfig::System::GetInstance();
//
// red::DynArray< String > resolutions{ ink::PoolInk_System() };
// // Populate resolutions, set current and default one
// resolutions.PushBack( "720x480" );
// resolutions.PushBack( "720x576" );
// resolutions.PushBack( "1176x664" );
// resolutions.PushBack( "1280x720" );
// resolutions.PushBack( "1920x1080" );
//
// const Int32 currentResolution = 4;
// const Int32 defaultResolution = 2;
//
// const CName varResolutionName = RED_NAME_CONSTEXPR( "DisplayResolution" );
//
// if ( config.HasVar( s_settingsGroup, varResolutionName ) )
// {
// 		auto &displayResolutions = config.GetVarListString( s_settingsGroup, varResolutionName );
// 		displayResolutions.SetValues( resolutions, false );
// 		displayResolutions.SetDefaultIndex( defaultResolution );
// 		displayResolutions.SetIndex( currentResolution, false );
// }
// else
// {
// 		auto displayResolutions = red::CreateUniquePtr< InGameConfig::VarListString >(
// 			red::AbsolutePath(),
// 			s_settingsGroup,
// 			varResolutionName,
// 			"Display Resolution",
// 			"Display Resolution",
// 			InGameConfig::VarUpdatePolicy::ConfirmationRequired,
// 			InGameConfig::VarFlags_Visible,
// 			0,
// 			currentResolution,
// 			defaultResolution,
// 			resolutions );
// 		auto &controlsGroup = config.GetGroup( s_settingsGroup );
// 		controlsGroup.AddVar( std::move( displayResolutions ) );
// }
//
