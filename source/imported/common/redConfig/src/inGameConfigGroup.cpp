/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "inGameConfigGroup.h"
#include "inGameConfigVar.h"

namespace InGameConfig
{
	Group::~Group() = default;

	Group::Group( CName parentGroupPath, CName groupPath, CName name, CName displayName, Int32 order )
		: m_parentGroupPath( parentGroupPath )
		, m_groupPath( groupPath )
		, m_name( name )
		, m_displayName( displayName )
		, m_groups( red::PoolEngine() )
		, m_options( red::PoolEngine() )
		, m_order( order )
	{}

	CName Group::GetParentPath() const
	{
		return m_parentGroupPath;
	}

	CName Group::GetPath() const
	{
		return m_groupPath;
	}

	CName Group::GetName() const
	{
		return m_name;
	}

	CName Group::GetDisplayName() const
	{
		return m_displayName;
	}

	Bool Group::HasGroups() const
	{
		return !m_groups.Empty();
	}

	Bool Group::HasVars( const Int32 queryFlags ) const
	{
		for ( const auto &entry : m_options )
		{
			const auto &var = *entry.Value();

			if ( !FilterVar( var, queryFlags ) )
			{
				return true;
			}
		}

		return false;
	}

	Bool Group::IsEmpty( const Int32 queryFlags ) const
	{
		return !HasGroups() && !HasVars( queryFlags );
	}

	Bool Group::HasVar( CName name ) const
	{
		return m_options.Find( name ) != m_options.End();
	}

	Var& Group::GetVar( CName name )
	{
		auto it = m_options.Find( name );
		ALWAYSENABLED_RED_FATAL_ASSERT( it != m_options.End(), "Could not find in-game config var '%s' in group '%s'", name.AsChar(), m_groupPath.AsChar() );
		return *it.Value();
	}

	const Var& Group::GetVar( CName name ) const
	{
		auto it = m_options.Find( name );
		ALWAYSENABLED_RED_FATAL_ASSERT( it != m_options.End(), "Could not find in-game config var '%s' in group '%s'", name.AsChar(), m_groupPath.AsChar() );
		return *it.Value();
	}

	VarBool& Group::GetVarBool( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Bool, "Expected 'bool' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarBool&>( var );
	}

	const VarBool& Group::GetVarBool( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Bool, "Expected 'bool' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarBool&>( var );
	}

	VarInt& Group::GetVarInt( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Int, "Expected 'int' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarInt&>( var );
	}

	const VarInt& Group::GetVarInt( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Int, "Expected 'int' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarInt&>( var );
	}

	VarFloat& Group::GetVarFloat( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Float, "Expected 'float' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarFloat&>( var );
	}

	const VarFloat& Group::GetVarFloat( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Float, "Expected 'float' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarFloat&>( var );
	}

	VarName& Group::GetVarName( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Name, "Expected 'name' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarName&>( var );
	}
	
	const VarName& Group::GetVarName( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::Name, "Expected 'name' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarName&>( var );
	}

	VarListInt& Group::GetVarListInt( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::IntList, "Expected 'int_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarListInt&>( var );
	}

	const VarListInt& Group::GetVarListInt( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::IntList, "Expected 'int_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarListInt&>( var );
	}

	VarListFloat& Group::GetVarListFloat( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::FloatList, "Expected 'float_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarListFloat&>( var );
	}

	const VarListFloat& Group::GetVarListFloat( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::FloatList, "Expected 'float_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarListFloat&>( var );
	}

	VarListString& Group::GetVarListString( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::StringList, "Expected 'string_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarListString&>( var );
	}

	const VarListString& Group::GetVarListString( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::StringList, "Expected 'string_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarListString&>( var );
	}

	VarListName& Group::GetVarListName( CName configVar )
	{
		Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::NameList, "Expected 'name_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<VarListName&>( var );
	}

	const VarListName& Group::GetVarListName( CName configVar ) const
	{
		const Var& var = GetVar( configVar );
		ALWAYSENABLED_RED_FATAL_ASSERT( var.GetType() == VarType::NameList, "Expected 'name_list' type for in-game config var '%hs'", var.GetName().AsChar() );
		return static_cast<const VarListName&>( var );
	}

	red::DynArray< Var* > Group::GetVars( const Int32 queryFlags ) const
	{
		red::DynArray< Var* > result{ PoolInGameConfig() };

		for ( const auto& var : m_options )
		{
			auto& configVar = *var.Value();

			if ( FilterVar( configVar, queryFlags ) )
			{
				continue;
			}

			result.PushBack( &configVar );
		}

		SortVars( result );

		return result;
	}

	void Group::AddGroup( CName groupPath )
	{
		red::alg::PushBackUnique( m_groups, groupPath );
	}

	void Group::AddVar( red::UniquePtr< Var > configVar )
	{
		CName name = configVar->GetName();
		ALWAYSENABLED_RED_FATAL_ASSERT( m_options.Find( name ) == m_options.End(), "In-game config var '%s' is already registered in group '%s'", name.AsChar(), m_groupPath.AsChar() );
		m_options[name] = std::move( configVar );
	}

	void Group::MarkAsSaved()
	{
		for ( const auto &varEntry : m_options )
		{
			varEntry.Value()->InternalMarkAsSaved();
		}
	}

	void Group::MarkAsNeedRestoreToDefault()
	{
		for ( const auto &varEntry : m_options )
		{
			varEntry.Value()->InternalMarkAsNeedRestoreToDefault();
		}
	}

	Bool Group::RestoreToDefaults(
		const Source source,
		const Bool isPreGame,
		const Bool onlyVisible,
		const Bool onlyMarked )
	{
		const Int32 c_queryFlags = QueryFlags::QF_CanBeRestoredToDefault
			| ( isPreGame ? QueryFlags::QF_InPreGame : QueryFlags::QF_InGame )
			| ( onlyVisible ? QueryFlags::QF_Visible : 0 )
			| ( onlyMarked ? QueryFlags::QF_MarkedAsRestoreToDefault : 0 );

		Bool restored{};

		for ( const auto &varEntry : m_options )
		{
			if ( !FilterVar( *varEntry.Value(), c_queryFlags ) )
			{
				restored |= varEntry.Value()->RestoreDefault( source );

				if ( onlyMarked )
				{
					varEntry.Value()->InternalResetNeedRestoreToDefault();
				}
			}
		}

		return restored;
	}

	Bool Group::WasModifiedSinceLastSave() const
	{
		for ( const auto &varEntry : m_options )
		{
			const auto &var = varEntry.Value();
			if ( var->HasRequestedValue() || var->WasModifiedSinceLastSave() )
			{
				return true;
			}
		}

		return false;
	}

	void Group::SortVars( red::DynArray< Var* > &vars ) const
	{
		std::sort( vars.Begin(), vars.End(), [ this ]( Var *left, Var *right )
		{
			const Int32 order1 = left->GetOrder();
			const Int32 order2 = right->GetOrder();

			if ( order1 >= 0 && order2 >= 0 )
			{
				return order1 < order2;
			}
			else if ( order1 >= 0 )
			{
				return true;
			}
			else if ( order2 >= 0 )
			{
				return false;
			}

			return false;
		} );
	}

	Bool Group::FilterVar( const Var &var, Int32 queryFlags ) const
	{
		if ( queryFlags == QF_NoFilter )
		{
			return false;
		}

		const Bool flagVisible = ( queryFlags & QF_Visible ) == QF_Visible;
		const Bool flagInPreGame = ( queryFlags & QF_InPreGame ) == QF_InPreGame;
		const Bool flagInGame = ( queryFlags & QF_InGame ) == QF_InGame;
		const Bool flagPlatformSpecific = ( queryFlags & QF_PlatformSpecific ) == QF_PlatformSpecific;
		const Bool flagCommon = ( queryFlags & QF_Common ) == QF_Common;
		const Bool flagCanBeRestoredToDefault = ( queryFlags & QF_CanBeRestoredToDefault ) == QF_CanBeRestoredToDefault;
		const Bool flagMarkedAsRestoredToDefault = ( queryFlags & QF_MarkedAsRestoreToDefault ) == QF_MarkedAsRestoreToDefault;

		if ( flagVisible && !var.IsVisible() )
		{
			return true;
		}

		if ( flagInPreGame && !var.IsInPreGame() )
		{
			return true;
		}

		if ( flagInGame && !var.IsInGame() )
		{
			return true;
		}

		if ( flagPlatformSpecific && !var.IsPlatformSpecific() )
		{
			return true;
		}

		if ( flagCommon && var.IsPlatformSpecific() )
		{
			return true;
		}

		if ( flagCanBeRestoredToDefault && !var.CanBeRestoredToDefault() )
		{
			return true;
		}

		if ( flagMarkedAsRestoredToDefault && !var.MarkedAsNeedRestoreToDefault() )
		{
			return true;
		}

		return false;
	}

}
