/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"

namespace tools
{
	// helper function for deep-copying objects.
	// note: object hierarchy won't be parented properly until created object is saved.
	extern RED_REFLECTION_API THandle< ISerializable > CloneObject( const THandle< ISerializable >& sourceObject );
	extern RED_REFLECTION_API THandle< ISerializable > CopyObject( const THandle< ISerializable >& sourceObject );

	// Copy object properties between two objects. The destination object must have class compatible with the source object (identical class or derived class).
	extern RED_REFLECTION_API void CopyObjectProperties( const THandle< ISerializable >& sourceObject, const THandle< ISerializable >& destinationObject );

	class EditorObjectIDPath;
}

namespace world
{
	struct RED_REFLECTION_API CloneContext
	{
		CloneContext( const tools::EditorObjectIDPath&, const tools::EditorObjectIDPath&, const red::DynArray< tools::EditorObjectIDPath >&, const red::DynArray< tools::EditorObjectIDPath >& );

		const tools::EditorObjectIDPath& m_sourceObjectIDPath;
		const tools::EditorObjectIDPath& m_targetObjectIDPath;
		const red::DynArray< tools::EditorObjectIDPath >& m_sourceObjectIDPaths;
		const red::DynArray< tools::EditorObjectIDPath >& m_targetObjectIDPaths;
	};
}
