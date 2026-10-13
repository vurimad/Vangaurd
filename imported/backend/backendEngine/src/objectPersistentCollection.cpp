/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "objectPersistentCollection.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IObjectPersistentCollection, tools );
RTTI_END_TYPE();


using red::DynArray;

namespace tools
{

	IObjectPersistentCollection::~IObjectPersistentCollection()
	{
	}

	ObjectPersistentCollectionPtr IObjectPersistentCollection::CreateFromBuffer( const void* data, const Uint32 dataSize )
	{
		// empty data
		if ( !data || !dataSize )
			return ObjectPersistentCollectionPtr();

		// get current list of RTTI classes that implement the collection mechanics
		red::DynArray< const rtti::ClassType* > collectionClasses;
		GetRttiSystem().EnumClasses( ClassID< IObjectPersistentCollection >(), collectionClasses );

		// try every type
		// NOTE: the collections must be written in a way that ensures that the safety of this operation
		for ( const auto* collectionClass : collectionClasses )
		{
			const auto* defaultPtr = collectionClass->GetDefaultObject<IObjectPersistentCollection>(); 
			if ( defaultPtr->CanLoadFrom( data, dataSize ) )
			{
				IObjectPersistentCollection* ptr = collectionClass->CreateObject<IObjectPersistentCollection>();
				if ( ptr->Load( data, dataSize ) )
					return ObjectPersistentCollectionPtr( ptr );
				else
					RED_DELETE( ptr );
			}
		}

		// no collection restored
		return ObjectPersistentCollectionPtr();
	}

} // tools