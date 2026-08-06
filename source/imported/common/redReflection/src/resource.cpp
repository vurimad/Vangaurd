/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "resource.h"
#include "resourceCommon.h"
#include "gatheredResource.h"
#include "resourceMonitor.h"
#include "objectUtils.hpp"

using red::DynArray;
using red::String;

RTTI_BEGIN_ABSTRACT_TYPE( CResource );
	RTTI_PARENT_TYPE( ISerializable );
#ifndef RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_metadata ).notCooked();
#endif // RED_CONFIGURATION_FINAL
	RTTI_PROPERTY( m_cookingPlatform );
RTTI_END_TYPE();


const Uint32 c_invalidCreationId = ~0;

CResource::CResource()
	: 
#ifndef RED_CONFIGURATION_FINAL
#ifdef RED_PLATFORM_WINPC
	m_creationTimeStamp( EngineTime::GetNow() )
#else
	m_creationTimeStamp()
#endif
	, m_monitor( &res::MonitorRouter::GetInstance() )
	, m_isModified( false )
	, m_creationId( c_invalidCreationId )
	,
#endif // RED_CONFIGURATION_FINAL
	m_cookingPlatform( PLATFORM_None )
{
}

CResource::~CResource()
{
#ifdef RED_LOGGING_ENABLED
	if( m_creationId != c_invalidCreationId )
	{
		//RED_LOG_CATEGORY( red::LoggerCategory::LoggerCategory_Resources, "-- %hs, %u", GetPath().ToDebugString(), m_creationId );
	}
#endif
}

String CResource::GetFriendlyName() const
{
	String name;

	if ( m_path.IsValid() )
	{
		name += GetClass()->GetName().AsChar();
		name += " \"";
		name += m_path.ToString();
		name += "\"";
	}
	else
	{
		name += "Unnamed ";
		name += GetClass()->GetName().AsChar();
	}

	return name;
}

const res::ResourcePath& CResource::GetPath() const
{
	return m_path;
}

void CResource::Cooker_SetPlatform( ECookingPlatform platform )
{
	RED_FATAL_ASSERT( m_cookingPlatform == ECookingPlatform::PLATFORM_None, "Error changing the cooked platform on an already cooked resource. Expected %u Was %u Setting %u", ECookingPlatform::PLATFORM_None, m_cookingPlatform, platform );
	m_cookingPlatform = platform;
}

void CResource::Cooker_ResetPlatform()
{
	m_cookingPlatform = ECookingPlatform::PLATFORM_None;
}

void CResource::Internal_SetPath( const res::ResourcePath & path )
{
	m_path = path;
}

const Bool CResource::IsModified() const 
{
#ifdef RED_CONFIGURATION_FINAL
	return false;
#else
	return m_isModified;
#endif // RED_CONFIGURATION_FINAL
}

Bool CResource::MarkModified()
{
#ifndef RED_CONFIGURATION_FINAL
	if( HasValidPath() )
	{
		m_monitor->OnModifiedResource( m_path );
		m_isModified = true;
		return true;
	}

	// Go up
	if ( CResource* parentRes = red::FindParent< CResource >( this ) )
	{
		return parentRes->MarkModified();
	}
#endif // RED_CONFIGURATION_FINAL

	return true;
}

Bool CResource::UnmarkModified()
{
#ifndef RED_CONFIGURATION_FINAL
	if( HasValidPath() )
	{
		m_monitor->OnUnmodifiedResource( m_path );
		m_isModified = false;
		return true;
	}

	// Go up
	if ( CResource* parentRes = red::FindParent< CResource >( this ) )
	{
		return parentRes->UnmarkModified();
	}
#endif // RED_CONFIGURATION_FINAL

	return true;
}

Bool CResource::CanModify()
{
	if( HasValidPath() )
	{
		return true;
	}

	if ( CResource* parentRes = red::FindParent< CResource >( this ) )
	{
		return parentRes->CanModify();
	}

	return false;
}

void CResource::GetAdditionalInfo( DynArray< String >& info ) const
{
	// nothing
}

void CResource::OnCheckDataErrors() const
{
}

void CResource::RecreateInternalRenderResources( Uint32 flag )
{
}

const rtti::ClassType* ResourceClassByExtension( const red::String& ext )
{
	red::DynArray< const rtti::ClassType* > classes{ red::PoolEngine() };
	GetRttiSystem().EnumClasses( CResource::GetStaticClass(), classes );

	for ( Uint32 i = 0; i < classes.Size(); ++i )
	{
		const auto & defaultObject = classes[i]->GetDefaultObject< CResource >();
		if ( ext.EqualsNC( defaultObject->GetExtension() ) ||
			 ext.EqualsNC( defaultObject->GetDeprecatedExtension() ) )
		{
			return classes[i];
		}
	}

	return nullptr;
}

Bool CResource::IsDefaultResource() const
{
	const res::GatheredResource* defaultResource = GetDefaultResource();

	if ( defaultResource )
	{
		return defaultResource->Get().Get() == this;
	}

	return false;
}

Uint32 CResource::GetGPUSize() const
{
	return 0;
}

res::GatheredResource* CResource::GetDefaultResource() const 
{ 
	return nullptr; 
}

bool CResource::HasValidPath() const
{
	return m_path.IsValid();
}

const EngineTime & CResource::GetCreationTimeStamp() const
{
#ifdef RED_CONFIGURATION_FINAL
	return EngineTime::ZERO;
#else
	return m_creationTimeStamp;
#endif // RED_CONFIGURATION_FINAL
}

void CResource::Internal_SetCreationId( Uint32 id )
{
#ifndef RED_CONFIGURATION_FINAL
	m_creationId = id;
#endif // RED_CONFIGURATION_FINAL
}

const job::Counter* CResource::HACK_GetPostLoadWaitCounter() const
{
	return nullptr;
}
