/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redContainers/include/hashMap.h"
#include "redConfigPool.h"
#include "inGameConfigVar.h"

namespace InGameConfig
{
	enum class Source : Int8;

	class RED_CONFIG_API Group : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( PoolInGameConfig );

	public:
		using ConfigVars = red::HashMap< CName, red::UniquePtr< Var > >;

		Group( CName parentGroupPath, CName groupPath, CName name, CName displayName, Int32 order );
		~Group();

		CName GetParentPath() const;
		CName GetPath() const;
		CName GetName() const;

		CName GetDisplayName() const;

		Bool HasGroups() const;
		Bool HasVars( Int32 queryFlags ) const;

		Bool IsEmpty( Int32 queryFlags ) const;

		Bool HasVar( CName name ) const;

		Var& GetVar( CName name );
		const Var& GetVar( CName name ) const;

		VarBool& GetVarBool( CName configVar );
		const VarBool& GetVarBool( CName configVar ) const;
		VarInt& GetVarInt( CName configVar );
		const VarInt& GetVarInt( CName configVar ) const;
		VarFloat& GetVarFloat( CName configVar );
		const VarFloat& GetVarFloat( CName configVar ) const;
		VarName& GetVarName( CName configVar );
		const VarName& GetVarName( CName configVar ) const;
		VarListInt& GetVarListInt( CName configVar );
		const VarListInt& GetVarListInt( CName configVar ) const;
		VarListFloat& GetVarListFloat( CName configVar );
		const VarListFloat& GetVarListFloat( CName configVar ) const;
		VarListString& GetVarListString( CName configVar );
		const VarListString& GetVarListString( CName configVar ) const;
		VarListName& GetVarListName( CName configVar );
		const VarListName& GetVarListName( CName configVar ) const;

		red::DynArray< CName >& GetGroups() { return m_groups; }
		const red::DynArray< CName >& GetGroups() const { return m_groups; }

		Int32 GetOrder() const { return m_order; }

		red::DynArray< Var* > GetVars( Int32 queryFlags ) const;

		void AddGroup( CName groupPath );
		void AddVar( red::UniquePtr< Var > configVar );

		void MarkAsSaved();
		void MarkAsNeedRestoreToDefault();

		Bool RestoreToDefaults( Source source, Bool isPreGame, Bool onlyVisible, Bool onlyMarked );

		Bool WasModifiedSinceLastSave() const;

	private:
		void SortVars( red::DynArray< Var* > &vars ) const;
		Bool FilterVar( const Var &var, Int32 queryFlags ) const;

		const CName m_parentGroupPath;
		const CName m_groupPath;
		const CName m_name;
		const CName m_displayName;
		red::DynArray< CName > m_groups;
		ConfigVars m_options;
		Int32 m_order;
	};
}