/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageHandleSerializer.h"
#include "packageSerializer.h"
#include "packageTable.h"
#include "packageReadStream.h"
#include "packageWriteStream.h"
#include "packageTableOfContent.h"
#include "packageVersion.h"
#include "serializable.h"

namespace red
{
	PackageHandleSerializer::PackageHandleSerializer()
	{
	}
		
	PackageHandleSerializer::~PackageHandleSerializer()
	{
	}

	void PackageHandleSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const SerializableHandle * handle = static_cast< const SerializableHandle * >( param.buffer );
		const SerializableHandle * referenceHandle = static_cast< const SerializableHandle * >( param.referenceValue );
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;
		if( handle->Get() )
		{
			index = context.table.MapObject( handle->Get(), referenceHandle ? referenceHandle->Get() : nullptr );
		}
		context.serializer << index;
	}
		
	void PackageHandleSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;
		
		if( context.packageVersion < c_packageVersionObjectIndex32bits )
		{
			Uint16 smallIndex = 0xffff;
			context.serializer >> smallIndex;
			if( smallIndex != 0xffff )
			{
				index = smallIndex;	
			}
		}
		else
		{
			context.serializer >> index;
		}
		
		SerializableHandle & handle = *static_cast< SerializableHandle * >( param.buffer );
		if( index != c_invalidObjectIndex )
		{
			context.table.UnmapObject( index, handle );
		}
		else
		{
			handle.Reset();		
		}
	}
		
	void PackageHandleSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;

		if( context.packageVersion < c_packageVersionObjectIndex32bits )
		{
			Uint16 smallIndex = 0xffff;
			context.inputStream.Read( &smallIndex, sizeof( smallIndex ) );
			if( smallIndex != 0xffff )
			{
				index = smallIndex;
			}
		}
		else
		{
			context.inputStream.Read( &index, sizeof( index ) );
		}

		if( index != c_invalidObjectIndex )
		{
			index = context.table.RemapObject( index );
		}
		
		context.outputStream.Write( &index, sizeof( index ) );
	}


	PackageWeakHandleSerializer::PackageWeakHandleSerializer()
	{
	}
		
	PackageWeakHandleSerializer::~PackageWeakHandleSerializer()
	{
	}

	void PackageWeakHandleSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const SerializableWeakHandle * weakHandle = static_cast< const SerializableWeakHandle * >( param.buffer );
		const SerializableWeakHandle * referenceWeakHandle = static_cast< const SerializableWeakHandle * >( param.referenceValue );
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;
		SerializableHandle handle = weakHandle->ToHandle();
		
		if( handle )
		{
			SerializableHandle referenceHandle =  referenceWeakHandle ? referenceWeakHandle->ToHandle() : nullptr;
			index = context.table.MapObject( handle.Get(),  referenceHandle.Get() );	
		}

		context.serializer << index;
	}
		
	void PackageWeakHandleSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;
		
		if(context.packageVersion < c_packageVersionObjectIndex32bits)
		{
			Uint16 smallIndex = 0xffff;
			context.serializer >> smallIndex;
			if( smallIndex != 0xffff )
			{
				index = smallIndex;
			}
		}
		else
		{
			context.serializer >> index;
		}
		
		SerializableWeakHandle & weakHandle = *static_cast< SerializableWeakHandle * >( param.buffer );
		
		if( index != c_invalidObjectIndex )
		{
			SerializableHandle handle = weakHandle.ToHandle();
			context.table.UnmapObject( index, handle );
			weakHandle = handle; // Handle could have been either assign for reading, but also Created! Therefor, it need to be rebound to be safe,
		}
		else
		{
			weakHandle.Reset();
		}
	}
		
	void PackageWeakHandleSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::ObjectIndex index = c_invalidObjectIndex;

		if( context.packageVersion < c_packageVersionObjectIndex32bits )
		{
			Uint16 smallIndex = 0xffff;
			context.inputStream.Read( &smallIndex, sizeof( smallIndex ) );
			if( smallIndex != 0xffff )
			{
				index = smallIndex;
			}
		}
		else
		{
			context.inputStream.Read( &index, sizeof( index ) );
		}

		if( index != c_invalidObjectIndex )
		{
			index = context.table.RemapObject( index );
		}

		context.outputStream.Write( &index, sizeof( index ) );
	}

}
