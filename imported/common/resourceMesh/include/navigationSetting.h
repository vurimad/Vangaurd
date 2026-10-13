/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace NavGen
{
	enum class NavmeshImpact : Uint16
	{
		Walkable = 0,
		Ignored,
		Blocking,
		Road,
		CrowdWalkable,
		Stairs,
		Drones,
	};

	struct RESOURCEMESH_API NavigationSetting
	{
		RTTI_DECLARE_TYPE(NavigationSetting)

		NavmeshImpact m_navmeshImpact = NavmeshImpact::Blocking;
	};
}


namespace TrafficGen
{
	enum class MeshImpact : Uint16
	{
		UseNavigation = 0,
		ForceIgnored,
		ForceBlocking,
	};

	enum class DynamicImpact : Uint16
	{
		Ignored = 0,
		Blocking,
	};
	
	struct RESOURCEMESH_API TrafficSetting
	{
		RTTI_DECLARE_TYPE( TrafficSetting );
	
		MeshImpact m_meshImpact = MeshImpact::UseNavigation;
	};

	struct RESOURCEMESH_API DynamicTrafficSetting
	{
		RTTI_DECLARE_TYPE( DynamicTrafficSetting );

		DynamicImpact m_impact = DynamicImpact::Ignored;
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE(NavmeshImpact, NavGen);
RTTI_DECLARE_ENUM_IN_NAMESPACE(MeshImpact, TrafficGen);
RTTI_DECLARE_ENUM_IN_NAMESPACE(DynamicImpact, TrafficGen);