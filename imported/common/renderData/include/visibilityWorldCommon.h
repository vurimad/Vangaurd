/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "visibilityOccluderInstance.h"

namespace vis
{

	// The way an occluder is used in a world. Can be used to determine appropriate importance, grouping, etc.
	enum class WorldOccluderType : Uint8
	{
		// Support for overriding types. Using this indicates that either some system-default type should be used, or
		// that some default should be inherited (e.g. a node can inherit the setting from a mesh resource).
		Default,

		// Occluder is disabled. This can be useful for a mesh node, with a mesh that has occluder data, but in a
		// particular case it shouldn't be an occluder.
		None,

		// Small detail object. Not limited to indoor/outdoor use, but assumed to be less important.
		Detail,

		// Less important interior of some generally large object. Generally will be inside an Exterior occluder.
		MinorInterior,

		// Important interior of some generally large object. Generally will be inside an Exterior occluder.
		MajorInterior,

		// Exterior of some generally large object. Likely to spatially contain other objects.
		Exterior,
	};


	RED_INLINE vis::EOccluderImportance MapWorldOccluderImportance( WorldOccluderType type )
	{
		switch (type)
		{
		case WorldOccluderType::Detail:			return vis::EOccluderImportance::Detail;
		case WorldOccluderType::MinorInterior:	return vis::EOccluderImportance::Detail;
		case WorldOccluderType::MajorInterior:	return vis::EOccluderImportance::Minor;
		case WorldOccluderType::Exterior:		return vis::EOccluderImportance::Major;

		// Invalid cases. Cannot map into an importance, should be dealt with before calling here.
		case WorldOccluderType::Default:
		case WorldOccluderType::None:
		default:
			RED_HALT("Invalid occluder type %d", type);
			return vis::EOccluderImportance::Detail;
		}
	}

}

RTTI_DECLARE_ENUM_IN_NAMESPACE( WorldOccluderType, vis );
