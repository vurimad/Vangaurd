/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"

namespace res { class ResourcePath; }

namespace red
{
	enum PackageResourceImportType : Int8;

	const Uint32 c_invalidObjectIndex = ~0;

	class RED_REFLECTION_API PackageTableOfContent
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:

		PackageTableOfContent();
		virtual ~PackageTableOfContent();

		typedef Uint16 NameIndex;
		typedef Uint32 ObjectIndex;
		typedef Uint16 ResourceIndex;

		RED_MOCKABLE NameIndex MapName( CName name );
		RED_MOCKABLE ResourceIndex MapResource( const res::ResourcePath & path, PackageResourceImportType importType );
		ObjectIndex MapObject( const ISerializable * object, const void * referenceObject );
		
		RED_MOCKABLE CName UnmapName( NameIndex index ) const;
		RED_MOCKABLE res::ResourcePath UnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const;
		void UnmapObject( ObjectIndex index, SerializableHandle & handle ) const;
		
		NameIndex RemapName( NameIndex );
		ResourceIndex RemapResource( ResourceIndex, PackageResourceImportType importType );
		ObjectIndex RemapObject( ObjectIndex index );

		const rtti::IType * AcquireType( CName name );

	private:

		virtual NameIndex OnMapName( CName name ) = 0;
		virtual ObjectIndex OnMapObject( const ISerializable * object,  const void * referenceObject ) = 0;
		virtual ResourceIndex OnMapResource( const res::ResourcePath & path, PackageResourceImportType importType ) = 0;

		virtual CName OnUnmapName( NameIndex index ) const = 0;
		virtual void OnUnmapObject( ObjectIndex index, SerializableHandle& handle ) const = 0;
		virtual res::ResourcePath OnUnmapResource( ResourceIndex index, res::ResourceTokenHandle & token ) const = 0;

		virtual NameIndex OnRemapName( NameIndex index ) = 0;
		virtual ResourceIndex OnRemapResource( ResourceIndex index, PackageResourceImportType importType ) = 0;
		virtual ObjectIndex OnRemapObject( ObjectIndex index ) = 0;

		red::HashMap< CName, const rtti::IType * > m_typeDictionary;
	};

	RED_INLINE PackageTableOfContent::NameIndex PackageTableOfContent::MapName( CName name )
	{
		return OnMapName( name );
	}

	RED_INLINE PackageTableOfContent::ObjectIndex PackageTableOfContent::MapObject( const ISerializable* object, const void* referenceObject )
	{
		return OnMapObject( object, referenceObject );
	}

	RED_INLINE PackageTableOfContent::ResourceIndex PackageTableOfContent::MapResource( const res::ResourcePath& path, PackageResourceImportType importType )
	{
		return OnMapResource( path, importType );
	}

	RED_INLINE CName PackageTableOfContent::UnmapName( NameIndex index ) const
	{
		return OnUnmapName( index );
	}

	RED_INLINE void PackageTableOfContent::UnmapObject( ObjectIndex index, SerializableHandle& handle ) const
	{
		return OnUnmapObject( index, handle );
	}

	RED_INLINE res::ResourcePath PackageTableOfContent::UnmapResource( ResourceIndex index, res::ResourceTokenHandle& token ) const
	{
		return OnUnmapResource( index, token );
	}

	RED_INLINE PackageTableOfContent::ObjectIndex PackageTableOfContent::RemapObject( ObjectIndex index )
	{
		return OnRemapObject( index );
	}

	RED_INLINE PackageTableOfContent::NameIndex PackageTableOfContent::RemapName( NameIndex index )
	{
		return OnRemapName( index );
	}

	RED_INLINE PackageTableOfContent::ResourceIndex PackageTableOfContent::RemapResource( ResourceIndex index, PackageResourceImportType importType )
	{
		return OnRemapResource( index, importType );
	}
};
