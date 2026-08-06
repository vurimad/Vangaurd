/**
 * Copyright (c) 2019-2021 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace InGameConfig
{
	enum class VarType : Uint8
	{
		Bool,
		Int,
		Float,
		Name,
		IntList,
		FloatList,
		StringList,
		NameList
	};

	enum class VarUpdatePolicy : Uint8
	{
		Disabled,
		Immediately,
		ConfirmationRequired,
		RestartRequired,
		LoadLastCheckpointRequired
	};

	enum class VarImportPolicy : Uint8
	{
		ReadValue,
		Ignore // Value is not read from user settings save file, assumes it will be set from the code instead; always return false on WasModifiedSinceLastSave
	};

	enum VarFlags : Uint16
	{
		VF_InPreGame				= RED_FLAG( 0 ),
		VF_InGame					= RED_FLAG( 1 ),
		VF_Visible					= RED_FLAG( 2 ),
		VF_PlatformSpecific			= RED_FLAG( 3 ),
		VF_Dynamic					= RED_FLAG( 4 ),
		VF_DynamicHasLoadedValue	= RED_FLAG( 5 ),
		VF_DynamicInitialized		= RED_FLAG( 6 ),
		VF_ListHasDisplayValues		= RED_FLAG( 7 ),
		VF_CanBeRestoredToDefault	= RED_FLAG( 8 ),
		VF_MarkedAsRestoreToDefault = RED_FLAG( 9 ),
		VF_IsInput					= RED_FLAG( 10 ),
		VF_Disabled					= RED_FLAG( 11 ),
	};

	enum QueryFlags : Uint8
	{
		QF_NoFilter					= RED_FLAG( 0 ),
		QF_Visible					= RED_FLAG( 1 ),
		QF_InPreGame				= RED_FLAG( 2 ),
		QF_InGame					= RED_FLAG( 3 ),
		QF_PlatformSpecific			= RED_FLAG( 4 ),
		QF_Common					= RED_FLAG( 5 ),
		QF_CanBeRestoredToDefault	= RED_FLAG( 6 ),
		QF_MarkedAsRestoreToDefault	= RED_FLAG( 7 ),
	};

	enum class UserSettingsLoadStatus : Uint8
	{
		NotLoaded,
		InternalError,
		FileIsMissing,
		FileIsCorrupted,
		Loaded,
		ImportedFromOldVersion,
	};

	enum class UserSettingsSaveStatus : Uint8
	{
		NotSaved,
		InternalError,
		Saved,
	};

	enum class NotificationType : Uint8
	{
		RestartRequiredConfirmed,
		RestartRequiredRejected,
		ChangesApplied,
		ChangesRejected,
		ChangesLoadLastCheckpointApplied,
		ChangesLoadLastCheckpointRejected,
		Saved,
		ErrorSaving,
		Loaded,
		LoadCanceled,
		LoadInternalError,
		Refresh,
		LanguagePackInstalled
	};

	enum class Source : Int8
	{
		Invalid = -1,
		LoadSettings,
		Code,
		Settings,
		PresetCode,
		PresetSettings,
	};

	enum class Mode : Bool
	{
		Headless,
		Game,
	};

	enum class ChangeReason : Int8
	{
		Invalid = -1,
		Accepted,
		Rejected,
		NeedsConfirmation,
		NeedsRestart,
		NeedsLoadLastCheckpoint
	};

	enum class CanSave : Int8
	{
		No,
		ProcessingUserSettings,
		Yes,
	};

#ifndef RED_CONFIGURATION_FINAL
	void RED_CONFIG_API DebugDumpVar( const String& prefix, CName groupPath, CName varName, InGameConfig::VarType varType );
#endif
}
