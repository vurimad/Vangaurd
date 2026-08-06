/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "resourceAsyncReference.h"
#include "serializationMapping.h"
#include "resource.h"
#include "resourceLoader.h"
#include "stringSerialization.h"
#include "../../redFileSystem/include/file.h"
#include "../../redJobs2/include/jobShim.h"
#include "../../redFileSystem/include/fileVersionList.h"
#include "resourceToken.h"

namespace res
{
	static_assert( sizeof( ResourceAsyncReference ) == sizeof( ResourcePath ), "res::ResourceAsyncReference is too big" );

	ResourceAsyncReference::ResourceAsyncReference() = default;

	ResourceAsyncReference::ResourceAsyncReference( const ResourceAsyncReference& other )
		: m_path( other.m_path )
	{
	}

	ResourceAsyncReference::ResourceAsyncReference( ResourceAsyncReference&& other )
		: m_path( std::move( other.m_path ) )
	{
	}

	ResourceAsyncReference::ResourceAsyncReference( const ResourcePath& path )
		: m_path( path )
	{
	}

	ResourceAsyncReference& ResourceAsyncReference::operator=( const ResourceAsyncReference& other )
	{
		ResourceAsyncReference( other ).Swap( *this );
		return *this;
	}

	ResourceAsyncReference& ResourceAsyncReference::operator=( ResourceAsyncReference&& other )
	{
		ResourceAsyncReference( std::move( other ) ).Swap( *this );
		return *this;
	}

	ResourceAsyncReference::~ResourceAsyncReference()
	{
	}

	void ResourceAsyncReference::SetPath( const ResourcePath& path )
	{
		m_path = path;
	}

	ResourceTokenHandle ResourceAsyncReference::IssueLoadingRequest(io::EAsyncPriority priority) const
	{
		LoadingOptions options;
		return IssueLoadingRequest( options, priority );
	}

	ResourceTokenHandle ResourceAsyncReference::IssueLoadingRequest( const LoadingOptions & options, io::EAsyncPriority priority) const
	{
		const io::EAsyncPriority ioPriorityReadFromTLS = io::GetThreadLocalIOPriority();
		if ( ioPriorityReadFromTLS != io::eAsyncPriority_INVALID )
		{
			// this takes precedence over manually set priority
			priority = ioPriorityReadFromTLS;
		}

		if ( m_path.IsValid() )
		{
			auto param = Convert(m_path, options);
			param.priority = priority;
			return GResourceLoader->IssueLoadingRequest(param);
		}
		
		RED_FATAL( "Issuing a loading request on empty async reference will always fail. There's a bug in your logic." );
		return nullptr;
	}

	const Bool ResourceAsyncReference::CheckLoaded() const
	{
		return IssueLoadingRequest()->IsLoaded();
	}

	ResourceTokenHandle ResourceAsyncReference::IssueReloadingRequest() const
	{
		LoadingOptions options;
		return IssueReloadingRequest( options );
	}

	ResourceTokenHandle ResourceAsyncReference::IssueReloadingRequest( const LoadingOptions & options ) const
	{
		if ( m_path.IsValid() )
		{
			return GResourceLoader->IssueReloadingRequest( Convert( m_path, options ) );
		}

		RED_FATAL( "Issuing a reloading request on empty async reference will always fail. There's a bug in your logic." );
		return nullptr;
	}

	void ResourceAsyncReference::Serialize( IFile& file )
	{
		if ( file.HasMapper() )
		{
			// MAPPED SAVE
			serialization::IMapper::PathIndex index = 0;
			if ( file.IsWriter() )
			{
				file.GetMapper()->MapResourceDeferredReference( m_path, index );
				file << index;
			}
			else if ( file.IsReader() )
			{
				file << index;
				file.GetMapper()->UnmapResourceDeferredReference( index, m_path );
			}
		}
		else
		{
			// DIRECT SAVE, supported but slower
			if ( file.IsWriter() )
			{
				Uint64 hash = m_path.GetHash();
				file << hash;
			}
			else if ( file.IsReader() )
			{
				if ( file.GetVersion() >= VER_RESOURCEPATHS_WITHOUT_STRINGS )
				{
					Uint64 hash = 0ull;
					file << hash;
					m_path = ResourcePath::Build( hash );
				}
				else
				{
					String ansiPath;
					file << ansiPath;
					m_path = ResourcePath::Build( ansiPath );
				}
			}
		}
	}

	void ResourceAsyncReference::Swap( ResourceAsyncReference& other )
	{
		if ( this != &other )
		{
			std::swap( m_path, other.m_path );
		}
	}
	
} // res