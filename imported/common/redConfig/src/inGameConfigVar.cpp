/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigVar.h"
#include "inGameConfigSystem.h"

namespace InGameConfig
{
	template class VarRange< Int32, VarType::Int >;
	template class VarRange< Float, VarType::Float >;
	template class VarList< Int32, VarType::IntList >;
	template class VarList< Float, VarType::FloatList >;
	template class VarList< red::String, VarType::StringList >;
	template class VarList< CName, VarType::NameList >;

	Var::~Var() = default;

	Var::Var(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarType type,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order )
		: m_name( name )
		, m_group( groupPath )
		, m_displayName( displayName )
		, m_description( description )
		, m_type( type )
		, m_updatePolicy( updatePolicy )
		, m_importPolicy( importPolicy )
		, m_flags( flags )
		, m_order( order )
		, m_sourceOfChange( Source::Invalid )
		, m_reasonOfChange( ChangeReason::Invalid )
	{}

	void Var::InternalMarkAsNeedRestoreToDefault()
	{
		SetFlag( VF_MarkedAsRestoreToDefault );
	}

	void Var::InternalResetNeedRestoreToDefault()
	{
		ResetFlag( VF_MarkedAsRestoreToDefault );
	}

	void Var::ScheduleVarUpdateWithoutConfirmation()
	{
		System::GetInstance().ScheduleVarUpdateWithoutConfirmation( this );
	}

	void Var::ScheduleVarUpdateWithConfirmation()
	{
		System::GetInstance().ScheduleVarUpdateWithConfirmation( this );
	}

	void Var::ScheduleVarUpdateWithRestart()
	{
		System::GetInstance().ScheduleVarUpdateWithRestart( this );
	}

	void Var::ScheduleVarUpdateWithLoadLastCheckpoint()
	{
		System::GetInstance().ScheduleVarUpdateWithLoadLastCheckpoint( this );
	}

	void Var::ScheduleVarLoadFromSettings()
	{
		System::GetInstance().ScheduleVarLoadFromSettings( this );
	}

	void Var::SetVisible( const Bool visible )
	{
		if ( visible )
		{
			SetFlag( VF_Visible );
		}
		else
		{
			ResetFlag( VF_Visible );
		}
	}

	void Var::SetEnabled( Bool enabled )
	{
		if ( enabled )
		{
			ResetFlag( VF_Disabled );
		}
		else
		{
			SetFlag( VF_Disabled );
		}
	}

	void Var::ResetSourceOfChange()
	{
		m_sourceOfChange = Source::Invalid;
	}

	void Var::ResetReasonOfChange()
	{
		m_reasonOfChange = ChangeReason::Invalid;
	}

	VarBool::VarBool(
			CName groupPath,
			CName name,
			CName displayName,
			CName description,
			VarUpdatePolicy updatePolicy,
			VarImportPolicy importPolicy,
			Int32 flags,
			Int32 order,
			Bool value,
			Bool defaultValue )
		: Var( groupPath, name, displayName, description, VarType::Bool, updatePolicy, importPolicy, flags, order )
		, m_value( value )
		, m_defaultValue( defaultValue )
		, m_requestedValue( value )
		, m_lastSavedValue( value )
	{}

	void VarBool::SetValue( const Bool value, const Source source )
	{
		InternalSetValue( value, source );
	}

	void VarInt::SetValue( const Int32 value, const Source source )
	{
		InternalSetValue( value, source );
	}

	Bool VarInt::RestoreDefault( const Source source )
	{
		const Bool c_hasDefaultValue = HasDefaultValue();

		if ( !c_hasDefaultValue )
		{
			SetValue( GetDefaultValue(), source );
		}

		return !c_hasDefaultValue;
	}

	void VarFloat::SetValue( const Float value, const Source source )
	{
		InternalSetValue( value, source );
	}

	VarName::VarName(
		CName groupPath,
		CName name,
		CName displayName,
		CName description,
		VarUpdatePolicy updatePolicy,
		VarImportPolicy importPolicy,
		Int32 flags,
		Int32 order,
		CName value,
		CName defaultValue )
		: Var( groupPath, name, displayName, description, VarType::Name, updatePolicy, importPolicy, flags, order )
		, m_value( value )
		, m_defaultValue( defaultValue )
		, m_requestedValue( value )
		, m_lastSavedValue( value )
	{}

	void VarName::SetValue( CName value, const Source source )
	{
		InternalSetValue( value, source );
	}

	Bool VarFloat::RestoreDefault( const Source source )
	{
		const Bool c_hasDefaultValue = HasDefaultValue();

		if ( !c_hasDefaultValue )
		{
			SetValue( GetDefaultValue(), source );
		}

		return !c_hasDefaultValue;
	}
}
