/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#ifndef RED_CONFIGURATION_FINAL

#include "inGameConfigUtils.h"
#include "inGameConfigSystem.h"
#include "inGameConfigVar.h"

namespace InGameConfig
{
	void DebugDumpVar( const String& prefix, CName groupPath, CName varName, InGameConfig::VarType varType )
	{
		using InGameConfig::System;
		using InGameConfig::VarType;
		using InGameConfig::Var;
		using InGameConfig::VarBool;
		using InGameConfig::VarName;
		using InGameConfig::VarInt;
		using InGameConfig::VarFloat;
		using InGameConfig::VarListInt;
		using InGameConfig::VarListFloat;
		using InGameConfig::VarListString;
		using InGameConfig::VarListName;

		System &configSystem = System::GetInstance();

		switch ( varType )
		{
		case VarType::Bool:
		{
			const VarBool &var = configSystem.GetVarBool( groupPath, varName );
			RED_LOG_INFO( "[%hs][BOOL] group: %hs name: %hs order: %d value: %d default: %d requested: %d",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue(),
				var.GetDefaultValue(), var.GetRequestedValue() );
			break;
		}
		case VarType::Name:
		{
			const VarName &var = configSystem.GetVarName( groupPath, varName );
			RED_LOG_INFO( "[%hs][NAME] group: %hs name: %hs order: %d value: 0x%016llX default: 0x%016llX requested: 0x%016llX",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue().GetHash(),
				var.GetDefaultValue().GetHash(), var.GetRequestedValue().GetHash() );
			break;
		}
		case VarType::Int:
		{
			const VarInt &var = configSystem.GetVarInt( groupPath, varName );
			RED_LOG_INFO( "[%hs][INT] group: %hs name: %hs order: %d value: %d default: %d min: %d max: %d step: %d requested: %d",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue(),
				var.GetDefaultValue(), var.GetMinValue(), var.GetMaxValue(), var.GetStepValue(), var.GetRequestedValue() );
			break;
		}
		case VarType::Float:
		{
			const VarFloat &var = configSystem.GetVarFloat( groupPath, varName );
			RED_LOG_INFO( "[%hs][FLOAT] group: %hs name: %hs order: %d value: %f default: %f min: %f max: %f step: %f requested: %f",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue(),
				var.GetDefaultValue(), var.GetMinValue(), var.GetMaxValue(), var.GetStepValue(), var.GetRequestedValue() );
			break;
		}
		case VarType::IntList:
		{
			const VarListInt &var = configSystem.GetVarListInt( groupPath, varName );
			RED_LOG_INFO( "[%hs][INT_LIST] group: %hs name: %hs order: %d value: %d default: %d current idx: %d default idx: %d requested: %d",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue(),
				var.GetDefaultValue(), var.GetIndex(), var.GetDefaultIndex(), var.GetRequestedValue() );
			break;
		}
		case VarType::FloatList:
		{
			const VarListFloat &var = configSystem.GetVarListFloat( groupPath, varName );;
			RED_LOG_INFO( "[%hs][FLOAT_LIST] group: %hs name: %hs order: %d value: %f default: %f current idx: %d default idx: %d requested: %f",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue(),
				var.GetDefaultValue(), var.GetIndex(), var.GetDefaultIndex(), var.GetRequestedValue() );
			break;
		}
		case VarType::StringList:
		{
			const VarListString &var = configSystem.GetVarListString( groupPath, varName );
			RED_LOG_INFO( "[%hs][STRING_LIST] group: %hs name: %hs order: %d value: %hs default: %hs current idx: %d default idx: %d requested: %hs",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue().AsChar(),
				var.GetDefaultValue().AsChar(), var.GetIndex(), var.GetDefaultIndex(), var.GetRequestedValue().AsChar() );
			break;
		}
		case VarType::NameList:
		{
			const VarListName &var = configSystem.GetVarListName( groupPath, varName );
			RED_LOG_INFO( "[%hs][NAME_LIST] group: %hs name: %hs order: %d value: %hs default: %hs current idx: %d default idx: %d requested: %hs",
				prefix.AsChar(), var.GetGroup().AsChar(), var.GetName().AsChar(), var.GetOrder(), var.GetValue().AsStringView().Data(),
				var.GetDefaultValue().AsStringView().Data(), var.GetIndex(), var.GetDefaultIndex(), var.GetRequestedValue().AsChar() );
			break;
		}
		default:
			break;
		}
	}
}
#endif
