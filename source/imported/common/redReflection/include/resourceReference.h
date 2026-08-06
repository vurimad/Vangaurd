/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiUtils.h"
#include "handle.h"
#include "resourceToken.h"

class CResource;

namespace res
{
	class ResourcePath;
	
	// immediate (hard) reference to resource, persistent even if there's no resource backing it up
	// the referenced resource will be a dependency of the resource holding this reference and it will be loaded automatically
	class RED_REFLECTION_API ResourceReference
	{
	public:
		ResourceReference();
		ResourceReference( const ResourceReference& other );
		ResourceReference( ResourceReference&& other );
		explicit ResourceReference( const ResourcePath& path ); // NOTE: this does not load the resource and the next GetResource() will do it
		ResourceReference( const ResourceTokenHandle & token );
		ResourceReference( const THandle< CResource > & resource ); 
		ResourceReference& operator= ( const ResourceReference& other );
		ResourceReference& operator= ( ResourceReference&& other );
		~ResourceReference();

		// empty resource reference
		RED_FORCE_INLINE const Bool Empty() const { return !m_path.IsValid(); }

		// get resource path
		RED_FORCE_INLINE const ResourcePath& GetPath() const { return m_path; }

		// set resource path
		void SetPath( const ResourcePath& path );

		// do we have a valid resource reference ?
		RED_FORCE_INLINE const Bool IsValid() const { return m_token && m_token->IsLoaded(); }

		// get the loaded resource (may be NULL)
		// if a resource was not loaded for some reason it is loaded SYNCHRONOUSLY (very bad)
		const THandle< CResource > & Get() const;

		// get the loaded resource, will fatal assert if the resource is NOT valid (missing)
		// the Safe means that the code execution will NOT continue if there resource is not there
		const THandle< CResource > & GetSafe() const;

		// Serialize resource reference
		void Serialize( IFile& file );
		void HACK_SerializeForConversion( IFile& file ); 
		
		// Release resource and reset path
		void Clear();

		void Internal_SetResourceToken( const ResourceTokenHandle & token ); // ctremblay: Not sure how to this differently at this stage. Would be nice to having something less intrusive.
		
	protected:
		void Swap( ResourceReference& other );

	protected:
		void EnsureLoaded() const;
		
		ResourcePath m_path;				//!< Path to resource, if set then always valid
		mutable ResourceTokenHandle	m_token;
	};
} // res

// reference to resource, preserves the path even if the actual resource was not loaded
template< class T >
class TResRef : public res::ResourceReference
{
public:
	//! Create empty resource reference
	RED_FORCE_INLINE TResRef()
	{}

	RED_FORCE_INLINE TResRef( std::nullptr_t )
	{}

	//! Copy
	RED_FORCE_INLINE TResRef( const TResRef< T >& other )
		: res::ResourceReference( other )
	{}

	template <class Other>
	RED_FORCE_INLINE TResRef( const TResRef< Other >& other )
		: res::ResourceReference( other )
	{
		static_assert( std::is_base_of< T, Other >::value || std::is_base_of< Other, T >::value, "Cannot static cast unrelated type." );
	}

	//! Move
	RED_FORCE_INLINE TResRef( TResRef< T >&& other )
		: res::ResourceReference( other )
	{}

	template <class Other>
	RED_FORCE_INLINE TResRef( TResRef< Other >&& other )
		: res::ResourceReference( other )
	{
		static_assert(std::is_base_of< T, Other >::value || std::is_base_of< Other, T >::value, "Cannot static cast unrelated type.");
	}

	//! Setup from resource path, no resource will be automatically loaded - for that use the LoadResource
	RED_FORCE_INLINE TResRef( const res::ResourcePath& path )
		: res::ResourceReference( path )
	{}

	//! Setup from loaded resource
	RED_FORCE_INLINE TResRef( const THandle< CResource >& res )
		: res::ResourceReference( res )
	{}

	RED_FORCE_INLINE TResRef( const res::ResourceTokenHandle& token )
		: res::ResourceReference( token )
	{}

	//! Copy assignment
	RED_FORCE_INLINE TResRef< T >& operator=( const TResRef< T >& other )
	{
		TResRef< T >( other ).Swap( *this );
		return *this;
	}

	//! Set from resource handle
	RED_FORCE_INLINE TResRef< T >& operator=( const THandle< T >& other )
	{
		TResRef< T >( other ).Swap( *this );
		return *this;
	}

	RED_FORCE_INLINE TResRef< T >& operator=( std::nullptr_t )
	{
		TResRef< T >( nullptr ).Swap( *this );
		return *this;
	}

	//! Get the object from the handle
	//! Returns NULL if resource reference is not valid
	RED_FORCE_INLINE THandle< T > Get() const
	{
		return Cast< T >( res::ResourceReference::Get() );
	}

	//! Get the object from the handle
	//! Will fatal assert if the resource is NOT valid (missing)
	//! The Safe means that the code execution will NOT continue if there resource is not there
	RED_FORCE_INLINE THandle< T > GetSafe() const
	{
		return Cast<T>( res::ResourceReference::GetSafe() );
	}

	//! Access the object
	RED_FORCE_INLINE T* operator->() const
	{
		const auto & ptr = res::ResourceReference::GetSafe();
		return static_cast< T* >( ptr.Get() );
	}

	RED_FORCE_INLINE static const TResRef< T >& Null()
	{
		static TResRef< T > nullHandle( nullptr );
		return nullHandle;
	}

	//! Cast to boolean
	RED_FORCE_INLINE explicit operator bool() const
	{
		return IsValid();
	}
};

template< typename T >
RED_FORCE_INLINE void operator<<( IFile& file, class TResRef<T>& resRef )
{
	resRef.Serialize( file );
}

/// RTTI  binding
template < typename T >
struct TTypeName< TResRef< T > >
{
	static const CName GetTypeName()
	{
		static const CName name = rtti::FormatResRefTypeName( TTypeName< T >::GetTypeName() );
		return name;
	}
};

template < typename T >
RED_INLINE bool operator==( const TResRef< T >& left, const TResRef< T >& right )
{
	return left.Get() == right.Get();
}

template < typename T >
RED_INLINE bool operator!=( const TResRef< T >& left, const TResRef< T >& right )
{
	return !operator==( left, right );
}
