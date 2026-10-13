/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "navigationSetting.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( NavmeshImpact, NavGen );
	RTTI_ENUM_OPTION( Ignored );
	RTTI_ENUM_OPTION( Walkable );
	RTTI_ENUM_OPTION( Blocking );
	RTTI_ENUM_OPTION( Road );
	RTTI_ENUM_OPTION( CrowdWalkable );
	RTTI_ENUM_OPTION( Stairs ); //walkable, but stairs
	RTTI_ENUM_OPTION( Drones ); //drone specific
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( NavigationSetting, NavGen );
	RTTI_PROPERTY(m_navmeshImpact).instanceEditable();
RTTI_END_TYPE();

RTTI_BEGIN_ENUM_IN_NAMESPACE( MeshImpact, TrafficGen );
	RTTI_ENUM_OPTION( UseNavigation );
	RTTI_ENUM_OPTION( ForceIgnored );
	RTTI_ENUM_OPTION( ForceBlocking );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE(DynamicImpact, TrafficGen);
	RTTI_ENUM_OPTION(Ignored);
	RTTI_ENUM_OPTION(Blocking);
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TrafficSetting, TrafficGen );
	RTTI_PROPERTY( m_meshImpact ).instanceEditable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE(DynamicTrafficSetting, TrafficGen);
	RTTI_PROPERTY(m_impact).instanceEditable();
RTTI_END_TYPE();
