/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourcePath.h"
#include "rttiUtils.h"
#include "resourceLoaderTypes.h"

namespace res
{
	struct LoadingOptions;

	class RED_REFLECTION_API ResourceAsyncReference
	{
	public:
		ResourceAsyncReference();
		ResourceAsyncReference( const ResourceAsyncReference& other );
		ResourceAsyncReference( ResourceAsyncReference&& other );
		explicit ResourceAsyncReference( const ResourcePath& path ); // NOTE: this does not load the resource
		ResourceAsyncReference& operator= ( const ResourceAsyncReference& other );
		ResourceAsyncReference& operator= ( ResourceAsyncReference&& other );

		~ResourceAsyncReference();

		// empty resource reference
		RED_FORCE_INLINE const Bool Empty() const { return !m_path.IsValid(); }

		// get resource path
		RED_FORCE_INLINE const ResourcePath& GetPath() const { return m_path; }

		// set resource path
		void SetPath( const ResourcePath& path );

		// do we have a valid resource path
		RED_FORCE_INLINE const Bool IsValid() const { return m_path.IsValid(); }

		// check if the disk resource was already loaded
		const Bool CheckLoaded() const;
		
		// get resource loading token, it also starts loading
		ResourceTokenHandle IssueLoadingRequest(io::EAsyncPriority priority = io::eAsyncPriority_Normal) const;
		ResourceTokenHandle IssueLoadingRequest(const LoadingOptions & options, io::EAsyncPriority priority = io::eAsyncPriority_Normal) const;

		ResourceTokenHandle IssueReloadingRequest() const;
		ResourceTokenHandle IssueReloadingRequest( const LoadingOptions & options ) const;

		// Serialize resource reference to a file
		void Serialize( IFile& file );

	protected:
		void Swap( ResourceAsyncReference& other );
		
	private:
		ResourcePath m_path; //!< Path to resource, if set then resource is always valid
	};
} // res

/// async reference to resource, preserves the path even if the actual resource was not loaded
template< class T >
class TResAsyncRef : public res::ResourceAsyncReference
{
public:
	//! Create empty resource reference
	RED_FORCE_INLINE TResAsyncRef()
		: res::ResourceAsyncReference()
	{}

	//! Copy
	RED_FORCE_INLINE TResAsyncRef( const TResAsyncRef< T >& other )
		: res::ResourceAsyncReference( other )
	{}

	//! Move
	RED_FORCE_INLINE TResAsyncRef( TResAsyncRef< T >&& other )
		: res::ResourceAsyncReference( other )
	{}

	//! Copy assignment
	RED_FORCE_INLINE TResAsyncRef< T >& operator=( const TResAsyncRef< T >& other )
	{
		TResAsyncRef< T >( other ).Swap( *this );
		return *this;
	}

	//! Setup from resource path, no resource will be automatically loaded - for that use the LoadResource
	RED_FORCE_INLINE TResAsyncRef( const res::ResourcePath& path )
		: res::ResourceAsyncReference( path )
	{}

	static const TResAsyncRef< T >& Null()
	{
		static TResAsyncRef< T > nullHandle;
		return nullHandle;
	}

	//! Cast to boolean
	RED_INLINE explicit operator bool() const
	{
		return IsValid();
	}
};

class IFile;

template< typename T >
RED_FORCE_INLINE void operator<<( IFile& file, class TResAsyncRef<T>& resRef )
{
	resRef.Serialize( file );
}

template < typename T >
struct TTypeName;

/// RTTI  binding
template < typename T >
struct TTypeName< TResAsyncRef< T > >
{
	static const CName GetTypeName()
	{
		static CName typeName = rtti::FormatResAsyncRefTypeName( TTypeName< T >::GetTypeName() );
		return typeName;
	}
};

