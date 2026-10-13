/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/serializable.h"
#include "../../redReflection/include/handle.h"


namespace vis
{

	enum class EOccluderImportance;

	class RENDER_DATA_API IOccluderResource : public ISerializable
	{
		RTTI_DECLARE_TYPE( IOccluderResource );
		RED_USE_MEMORY_POOL( rend::PoolOccluderShapes );

	protected:
		Uint32 m_resourceHash;

		IOccluderResource()
			: m_resourceHash(0)
		{}

	public:
		virtual ~IOccluderResource() {}

		virtual Uint32 GetVersion() const = 0;

		virtual Bool IsVersionValid() const = 0;

		virtual const math::Box& GetLocalBoundingBox() const = 0;
	};

}
