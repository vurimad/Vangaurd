/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageUtils.h"
#include "packageReader.h"
#include "serializable.h"
#include "packageInspector.h"
#include "packageBuilder.h"
#include "../../redContainers/include/blob.h"

namespace red
{
	CompiledPackage GeneratePackage( const ISerializable * rootObject )
	{		
		PackageBuilder builder;
		builder.Initialize( { PackagePropertyType_All } );
		builder.WriteObject( rootObject );
		return builder.BuildPackage();
	}

	CompiledPackage GeneratePackage( const GeneratePackageParameter & parameter )
	{
		RED_FATAL_ASSERT( parameter.rootObject, "Cannot create package from null object." );
		RED_FATAL_ASSERT( parameter.defaultObject, "Cannot create package without default object." );
		RED_FATAL_ASSERT( parameter.defaultObject->GetClass()->IsA( parameter.rootObject->GetClass() ), "Default Object need to be related to Root Object" );

		PackageBuilder builder;
		builder.Initialize( { parameter.flags } );
		builder.WriteObject( parameter.rootObject, parameter.defaultObject );
		return builder.BuildPackage();
	}
	
	CompiledPackage MergePackage( const Package & left, const Package & right )
	{
		PackageBuilder builder;
		builder.Initialize( { PackagePropertyType_All } );

		builder.AppendPackage( left );
		builder.AppendPackage( right );

		return builder.BuildPackage();
	}

	CompiledPackage RemovePackageEntry( const Package & package, Uint32 entry )
	{
		PackageBuilder builder;
		builder.Initialize( { PackagePropertyType_All } );
		builder.AppendPackage( package );
		builder.RemoveObject( entry );
		return builder.BuildPackage();
	}

	SerializableHandle CreateObject( const Package & package, Uint32 index )
	{
		if( index < package.objectTable.Size() )
		{
			const ObjectDescriptor & descriptor = package.objectTable[ index ];

			PackageTableOfContentView packageView( package );
			const CName objectTypeName = packageView.UnmapName( descriptor.typeNameIndex );
			SerializableHandle handle = CreateObject( objectTypeName );

			PackageInspector inspector;
			inspector.Initialize( package );
			inspector.ReadObject( *handle, index );
			return handle;
		}

		return nullptr;
	}

	SerializableHandle CreateObject( CName objectName )
	{
		const rtti::ClassType * type = GetRttiSystem().FindClass( objectName );
		if( type && type->IsSerializable() )
		{
			PC_SCOPE_INST_OBJ( type->GetInstrumentationObject(), type->GetName().AsChar() );
			SerializableHandle handle = type->CreateHandle< ISerializable >();
			return handle;
		}

		return nullptr;
	}
	
	void ApplyPackageContent( const Package & package, ISerializable & object, Uint32 index )
	{
		PackageInspector inspector;
		inspector.Initialize( package );
		inspector.ReadObject( object, index );
	}
}
