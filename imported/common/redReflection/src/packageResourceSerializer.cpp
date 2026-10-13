/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageResourceSerializer.h"
#include "packageSerializer.h"
#include "packageTableOfContent.h"
#include "packageReadStream.h"
#include "packageWriteStream.h"
#include "package.h"

#include "rttiPointerTypesImpl.h"
#include "resourceAsyncReference.h"

namespace red
{
	const PackageTableOfContent::ResourceIndex c_invalidResourceIndex = ~0;

	PackageResourceSerializer::PackageResourceSerializer()
	{}

	PackageResourceSerializer::~PackageResourceSerializer()
	{}

	void PackageResourceSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::ResourceReferenceType * resourceType = static_cast< const rtti::ResourceReferenceType * >( param.type );
		const res::ResourceReference * resourceReference = resourceType->GetResourceReference( param.buffer );

		const res::ResourcePath path = resourceReference->GetPath();
	
		const PackageTableOfContent::ResourceIndex index = path.IsValid() ?  
			context.table.MapResource( resourceReference->GetPath(), PackageResourceImportType_Sync ) : 
			c_invalidResourceIndex;

		context.serializer << index;
	}
	
	void PackageResourceSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ResourceIndex index = c_invalidResourceIndex;
		context.serializer >> index;
		
		res::ResourcePath path;
		const rtti::ResourceReferenceType * resourceType = static_cast< const rtti::ResourceReferenceType * >( param.type );
		res::ResourceReference * resourceReference = static_cast< res::ResourceReference * >( resourceType->GetResourceReference( param.buffer ) );
		if( index != c_invalidResourceIndex )
		{
			res::ResourceTokenHandle token;
			path = context.table.UnmapResource( index, token );
			if( token )
			{
				resourceReference->Internal_SetResourceToken( token );
			}
		}
		
		resourceReference->SetPath( path );
	}
	
	void PackageResourceSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ResourceIndex index = c_invalidResourceIndex;
		context.inputStream.Read( &index, sizeof( index ) );
		
		if( index != c_invalidResourceIndex )
		{
			index = context.table.RemapResource( index, PackageResourceImportType_Sync );
		}

		context.outputStream.Write( &index, sizeof( index ) );
	}

	PackageAsyncResourceSerializer::PackageAsyncResourceSerializer()
	{}

	PackageAsyncResourceSerializer::~PackageAsyncResourceSerializer()
	{}

	void PackageAsyncResourceSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::ResourceAsyncReferenceType * resourceType = static_cast< const rtti::ResourceAsyncReferenceType * >( param.type );
		const res::ResourceAsyncReference * resourceReference = resourceType->GetResourceReference( param.buffer );

		const res::ResourcePath path = resourceReference->GetPath();
	
		const PackageTableOfContent::ResourceIndex index = path.IsValid() ?  
			context.table.MapResource( resourceReference->GetPath(), PackageResourceImportType_Async ) : 
			c_invalidResourceIndex;

		context.serializer << index;
	}
	
	void PackageAsyncResourceSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ResourceIndex index = c_invalidResourceIndex;
		context.serializer >> index;
		
		res::ResourcePath path;
		const rtti::ResourceAsyncReferenceType * resourceType = static_cast< const rtti::ResourceAsyncReferenceType * >( param.type );
		res::ResourceAsyncReference * resourceReference = resourceType->GetResourceReference( param.buffer );
		if( index != c_invalidResourceIndex )
		{
			res::ResourceTokenHandle token;
			path = context.table.UnmapResource( index, token );
		}
		
		resourceReference->SetPath( path );
	}
	
	void PackageAsyncResourceSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ResourceIndex index = c_invalidResourceIndex;
		context.inputStream.Read( &index, sizeof( index ) );

		if(index != c_invalidResourceIndex)
		{
			index = context.table.RemapResource( index, PackageResourceImportType_Async );
		}

		context.outputStream.Write( &index, sizeof( index ) );
	}
}	
