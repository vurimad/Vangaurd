/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../../common/redContainers/include/redContainersPublic.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace Config
{
	class IConfigVar;

	/// Registry to IConsoleVars
	class RED_CONFIG_API CConfigVarRegistry
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		CConfigVarRegistry();
		~CConfigVarRegistry();

		// pull the config values from the given storage
		void Refresh( const class CConfigVarStorage& storage );

		// dump values that can be saved to a given storage
		void Capture( class CConfigVarStorage& storage ) const;

		// register variable in registry
		void Register( IConfigVar& var );

		// unregister variable from registry
		void Unregister( IConfigVar& var );

		// enumerate variables matching given search pattern
		void EnumVars( red::DynArray< IConfigVar* >& outVars, const AnsiChar* groupMatch = "", const AnsiChar* nameMatch = "", const Uint32 includeFlags = 0, const Uint32 excludeFlags = 0 ) const;

		// find variable
		IConfigVar* Find( const AnsiChar* groupName, const AnsiChar* name ) const;

	private:
		// name hashing
		typedef Uint32 TNameHash;
		static TNameHash CalcNameHash( const AnsiChar* name, const AnsiChar* groupName );

		// mapped variables
		typedef red::HashMap< TNameHash, IConfigVar* >	TConsoleVarMap;
		TConsoleVarMap		m_vars{ red::PoolEngine() };

		// thread safety lock (eh...)
		mutable red::Mutex	m_lock;
	};

} // Config