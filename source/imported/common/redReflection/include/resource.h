/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "datetime.h"
#include "deferredDataBuffer.h"
#include "reflectionPool.h"
#include "serializable.h"
#include "engineTime.h"
#include "util.h"

namespace res
{
	class ResourcePath;
	class MonitorRouter;
	class GatheredResource;
}


#define IMPLEMENT_RESOURCE_INTERFACE( deprecatedExt, ext, desc )						\
public:																					\
	virtual const char * GetDeprecatedExtension() const override	{ return deprecatedExt; }	\
	virtual const char * GetExtension() const override				{ return ext; }				\
	static const char * GetDeprecatedFileExtension()				{ return deprecatedExt; }	\
	static const char * GetFileExtension()							{ return ext; }				\
	virtual const char * GetFriendlyDescription() const override	{ return desc; }

// Base resource class
class RED_REFLECTION_API CResource : public ISerializable
{
	RTTI_DECLARE_TYPE( CResource );
	RED_USE_MEMORY_POOL( red::PoolResource );
	RED_BASE_CLASS( ISerializable );

public:

	CResource();
	virtual ~CResource();

	CResource( const CResource& ) = delete;
	CResource& operator=( const CResource& ) = delete;
	CResource( CResource&& ) = delete;
	CResource& operator=( CResource&& ) = delete;

	const Bool IsModified() const;
	virtual Bool MarkModified();
	virtual Bool UnmarkModified();
	virtual Bool CanModify();
	
	bool HasValidPath() const;
	virtual const res::ResourcePath& GetPath() const override final;

	virtual red::String GetFriendlyName() const;
	virtual const char* GetFriendlyDescription() const = 0;

	const EngineTime & GetCreationTimeStamp() const;

	// DEPRECATED AND NOT USED Validate this resource using the data error reporter
	virtual void OnCheckDataErrors() const;

	// Get additional resource info, displayed in editor
	virtual void GetAdditionalInfo( red::DynArray< red::String >& info ) const;

	// Get file extension used for this resource
	virtual const char* GetDeprecatedExtension() const = 0;
	virtual const char* GetExtension() const = 0;

	// How many bytes does this resource occupy on the GPU?
	virtual Uint32 GetGPUSize() const;

	// Get default resource used when resource of given type has not been found
	virtual res::GatheredResource* GetDefaultResource() const;

	// Check if the resource is default (fallback) resource
	Bool IsDefaultResource() const;

	// Get metadata buffer
#ifndef RED_CONFIGURATION_FINAL
	serialization::DeferredDataBuffer& GetMetadataBuffer() { return m_metadata; }
	const serialization::DeferredDataBuffer& GetMetadataBuffer() const { return m_metadata; }
#endif // RED_CONFIGURATION_FINAL

	virtual void RecreateInternalRenderResources( Uint32 flag );

	virtual Uint32 GetRenderResourceSize() const { return 0; }

	void Cooker_SetPlatform( ECookingPlatform platform );
	void Cooker_ResetPlatform();

	ECookingPlatform GetCookingPlatform() const { return m_cookingPlatform;  }

	void Internal_SetPath( const res::ResourcePath & path );
	void Internal_SetCreationId( Uint32 id );

	virtual const job::Counter* HACK_GetPostLoadWaitCounter() const;

private:
	res::ResourcePath m_path;
#ifndef RED_CONFIGURATION_FINAL
	EngineTime m_creationTimeStamp;
	const res::MonitorRouter * m_monitor; 
	serialization::DeferredDataBuffer m_metadata;
	Uint32 m_creationId; 
	bool m_isModified;
#endif // RED_CONFIGURATION_FINAL
	ECookingPlatform m_cookingPlatform;
};

RED_REFLECTION_API const rtti::ClassType* ResourceClassByExtension( const red::String& ext );

/// Get file extension used for particular resource class
template < class T >
RED_INLINE const char * ResourceExtension() 
{	
	const rtti::ClassType * classDesc = ClassID< T >();
	CResource * zeroRes = classDesc->GetDefaultObject< CResource >();
	return zeroRes->GetExtension();
}
