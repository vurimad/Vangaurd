/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{
	/// Captured data from group of object, the CORE or universal copy/paste
	class IObjectCapturedCollection
	{
	public:
		virtual ~IObjectCapturedCollection() {};

		/// Get type of stored content - this is the name from the object resovler used to create this collection;
		virtual const CName GetInternalName() const = 0;

		/// Get number of objects in the collection
		virtual const Uint32 GetSize() const = 0;

		/// Get global ID of the n-th object in the collection
		virtual const EditorObjectID GetGlobalID( const Uint32 objectIndex ) const = 0;

		/// Save to persistent storage (memory, disk, clipboard, etc)
		/// ALLOWED TO FAIL (although it's strongly recommenced to write a proper implementation)
		/// NOTE: the way the objects are stored should be really PERSISTENT and it should be possible to copy paste this data between two editors
		virtual void SaveToStorage( IFile& outputFile ) = 0;
	};

} // tools