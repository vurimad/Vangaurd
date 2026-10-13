/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace Config
{
	/// Helper class - holds a hierarchy of IConfigVars in their groups, used for easier browsing
	class RED_CONFIG_API CConfigVarHierarchy
	{
	public:
		class Group;

		class Entry
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			IConfigVar*			m_var;
			Group*				m_group;

			Entry( IConfigVar* var, Group* group );
		};

		class Group
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			Group* m_parent;
			String m_name;
			red::DynArray< Group* > m_children{ red::PoolEngine() };
			red::DynArray< Entry* > m_entries{ red::PoolEngine() };

		public:
			Group( Group* parent, const String& name );
			~Group();

			void Sort();
		};

		RED_INLINE Group* GetRoot() const { return m_root; }

		CConfigVarHierarchy();
		~CConfigVarHierarchy();

		void Reset();

	private:
		Group*		m_root;
	};

	/// Helper class - builds a hierarchy of config vars in their groups for easier browsing
	class CConfigVarHierarchyBuilder
	{
	public:
		CConfigVarHierarchyBuilder( CConfigVarHierarchy& outHierarchy );
		void BuildFromRegistry( const class CConfigVarRegistry& registry, const AnsiChar* nameFilter="", const Uint32 includedFlags = 0, const Uint32 excludedFlags = 0 );

	private:
		CConfigVarHierarchy*		m_hierarchy;

		CConfigVarHierarchy::Group* GetGroup( CConfigVarHierarchy::Group* parent, const String& name );
	};

} // Config