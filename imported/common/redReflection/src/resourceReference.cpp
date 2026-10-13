/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "resourceReference.h"
#include "resource.h"
#include "serializationMapping.h"
#include "resourceLoader.h"
#include "stringSerialization.h"

#include "../../redContainers/include/string/stringLocale.h"
#include "../../redFileSystem/include/file.h"
#include "../../redFileSystem/include/fileVersionList.h"

namespace res
{
	static_assert( sizeof( ResourceReference ) == sizeof( ResourcePath ) + sizeof( ResourceTokenHandle ), "res::ResourceReference is too big" );

	THandle< CResource > s_nullResource;
	
	ResourceReference::ResourceReference()
	{
	}

	ResourceReference::ResourceReference( const ResourceReference& other )
		: m_path( other.m_path )
		, m_token( other.m_token )
	{
	}

	ResourceReference::ResourceReference( const THandle< CResource > & resource )
	{
		if( resource )
		{
			m_path = resource->GetPath();
			if( m_path.IsValid() )
			{
				m_token = GResourceLoader->IssueLoadingRequest( { m_path } ); // ctremblay: How about resource keeps a weakptr of token instead?
			}
		}
	}

	ResourceReference::ResourceReference( const ResourceTokenHandle & token )
		: m_path( token->GetPath() )
		, m_token( token )
	{}

	ResourceReference::ResourceReference( ResourceReference&& other )
		: m_path( std::move( other.m_path ) )
		, m_token( std::move( other.m_token ) )
	{
		other.m_path = ResourcePath();
		other.m_token = nullptr;
	}

	ResourceReference::ResourceReference( const ResourcePath& path )
		: m_path( path )
	{
	}

	ResourceReference& ResourceReference::operator=( const ResourceReference& other )
	{
		ResourceReference( other ).Swap( *this );
		return *this;
	}

	ResourceReference& ResourceReference::operator=( ResourceReference&& other )
	{
		ResourceReference( std::move( other ) ).Swap( *this );
		return *this;
	}

	ResourceReference::~ResourceReference()
	{
	}

	void ResourceReference::Swap( ResourceReference& other)
	{
		if ( this != &other )
		{
			std::swap( m_path, other.m_path );
			std::swap( m_token, other.m_token );
		}
	}

	void ResourceReference::EnsureLoaded() const
	{
		if ( m_token && m_token->HasFinished() )
			return;

		// TODO: this is a very BAD situation for us, if this happens in game we need to investigate it
		if ( m_path.IsValid() )
		{
			m_token = GResourceLoader->IssueLoadingRequest( m_path );
			if ( !m_token->HasFailed() ) // may have been already loaded, so available right away without needing a job chain
			{
				m_token->WaitUntilLoaded();
			}
		}
	}

	void ResourceReference::SetPath( const ResourcePath& path )
	{
		m_path = path;
	}

	const THandle< CResource > & ResourceReference::Get() const
	{
		EnsureLoaded();
		return m_token ? m_token->GetResource() : s_nullResource;
	}

	const THandle< CResource > & ResourceReference::GetSafe() const
	{
		EnsureLoaded();
		RED_FATAL_ASSERT( m_token, "Missing critical resource: '%hs'", m_path.ToDebugString() );
		return m_token->GetResource();
	}

	void ResourceReference::Serialize( IFile& file )
	{
		if ( file.HasMapper() )
		{
			// MAPPED SAVE

			serialization::IMapper::PathIndex index = 0;
			if ( file.IsWriter() )
			{
				file.GetMapper()->MapResourceReference( m_path, index );
				file << index;
			}
			else if ( file.IsReader() )
			{
				file << index;
				file.GetMapper()->UnmapResourceReference( index, m_path, m_token );
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

				if ( m_token && m_token->GetPath() != m_path )
				{
					m_token = nullptr;
				}
			}
		}
	}

	void ResourceReference::HACK_SerializeForConversion( IFile& file )
	{
		RED_FATAL_ASSERT( file.IsReader() );

		if ( file.IsReader() )
		{
			serialization::IMapper::PathIndex index = 0;
			file << index;

			if( index < 0 )
			{
				index =  -(index);
				file.GetMapper()->UnmapResourceReference( index, m_path, m_token );
			}
			else if( index == 1)
			{
				// ctremblay: this will introduce a circular dependency. 
			}
			else
			{
				RED_FATAL( "Invalid Conversion. contact ctremblay." );
			}
		}
	}

	void ResourceReference::Clear()
	{
		m_path = ResourcePath();
		m_token.Reset();
	}

	void ResourceReference::Internal_SetResourceToken( const ResourceTokenHandle & token )
	{
		if( token )
		{
			m_token = token;
			m_path = token->GetPath();
		}
		else
		{
			Clear();
		}
	}

} // res

