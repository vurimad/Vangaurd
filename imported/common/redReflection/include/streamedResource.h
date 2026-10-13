/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "resource.h"
#include "mathBox.h"

struct Box;

namespace res
{
	class StreamingResourceManager;

	// TODO should be rather ResourceMetadata
	struct RED_REFLECTION_API StreamingData
	{
		StreamingData();

		Box m_boundingBox;				// overall bounding box, can't be empty
		Box m_visibleBoundingBox;		// bounding box of visible part only (e.g. will be empty for gameplay objects, lights, etc.)
		math::Vector3 m_surfaceAreaPerAxis;	// surface area of the mesh's triangles projected in X, Y and Z axis
		math::Vector3 m_streamingRefPosition; // streaming reference position offset (in resource's local space)
		Float m_autoHideDistance;		// distance at which a resource instance will start to be visible
		Float m_streamingDistance;		// distance at which a resource instance will be loaded
		CName m_entityClassType;
		Uint64 m_proxyHash;
		Uint32 m_triangleCount;			// number of rendered triangles in the resource
		Uint16 m_navmeshImpact = 0;			// enum stored by value to avoid includes

		// TEMP HACK - streamed resource cache must be refactored into "resource metadata cache"
		Bool m_castShadows = false;
		Bool m_castLocalShadows = false;
		Bool m_hasEmbeddedOccluder = false;

        // true if there is at least one terrain node in hierarchy
		Bool m_hasTerrainNodes = false;

		// true if there is at least one "[BUILDING]" (world::PrefabType::Building) in the hierarchy
		Bool m_hasBuildingPrefabNodes = false;

		Bool m_autoHideBoosted = false;

		// Has an advertisementWidgetComponent
		Bool m_isAdvertisementEntity = false;

		// TriggerActivatorComponent
		// PhysicalTriggerComponent
		// StaticTriggerAreaComponent
		//Bool m_isTriggerEntity = false;

#ifndef RED_CONFIGURATION_FINAL
		Bool m_isGIVisible = false;
#endif // RED_CONFIGURATION_FINAL


		Bool operator == ( const StreamingData& other ) const;
		Bool operator != ( const StreamingData& other ) const;

		RED_FORCE_INLINE Bool IsValid() const 
		{ 
			return m_streamingDistance > 0.0f && !m_boundingBox.IsEmpty(); 
		}
	};

	/// A resource that can be streamed in by distance
	/// We need to be able to extract streaming distance of a resource WITHOUT fully loading it hence the standardization
	class RED_REFLECTION_API StreamedResource : public CResource
	{
		RTTI_DECLARE_TYPE( StreamedResource );

	public:
		// Get streaming data for this resource
		// Note: this will access streaming manager
		StreamingData GetStreamingData() const;

		// called when streaming data gets changed due to external change
		virtual void OnStreamingDataChanged( const StreamingData& streamingData );

	protected:
		// notify "interested parties" that streaming distance for a resource has changed
		void NotifyStreamingDataChanged();

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual Bool ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const override;
	};

} // res