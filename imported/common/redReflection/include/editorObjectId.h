/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resourcePath.h"
#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"
#include "reflectionPool.h"

namespace tools
{
	/// internal ID of the editable object within the resource
	typedef Uint64 EditorObjectInternalID;

	/// Every editable object in the editor (content holder) has a GLOBAL ID
	/// this ID is used to identify object in various lists and sets
	/// It does not have to be persistent across sessions.
	/// It has following assumptions:
	///  1) assumes that everything we edit is based on some kind of a resource
	//   2) assumes that within the resource there is some kind of unique ID
	class RED_REFLECTION_API EditorObjectID
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		EditorObjectID();
		EditorObjectID( const EditorObjectID& other ) = default;
		EditorObjectID( EditorObjectID&& other ) = default;
		EditorObjectID& operator = ( const EditorObjectID& other ) = default;
		EditorObjectID& operator = ( EditorObjectID&& other ) = default;

		/// get the "kind", can be used to filter stuff
		const CName GetKind() const;

		/// get the resource path
		const res::ResourcePath& GetResourcePath() const;

		/// get internal ID of the object
		const EditorObjectInternalID GetInternalID() const;

		/// get node name
		const CName GetName() const;

		/// is this a valid ID ?
		const Bool IsValid() const;

		/// calculate hash for hashmap
		const Uint32 CalcHash() const;

		const Uint64 CalcHash64() const;

		// compare
		bool operator==( const EditorObjectID& other ) const;

		// compare
		bool operator!=( const EditorObjectID& other ) const;

		// less - maps/set
		bool operator<( const EditorObjectID& other ) const;

		/// Build editor object ID from resource path and internal ID of object
		static EditorObjectID Build( const res::ResourcePath& resourcePath, const EditorObjectInternalID internalId, const CName kind, const CName name = CName::NONE() );

		/// Non-resource based
		static EditorObjectID Build( const EditorObjectInternalID internalId, const CName kind, const CName name = CName::NONE() );

		//! Serialize to/from file
		void Serialize( IFile& file );

		//! Convert to string
		String ToString() const;

		//! Parse from string
		static Bool FromString(const String& str, EditorObjectID& outId);

	public:
		//! Serialize
		friend void operator<<( IFile& file, EditorObjectID& id )
		{
			id.Serialize( file );
		}

	private:
		res::ResourcePath m_resourcePath;		// path of the edited resource
		EditorObjectInternalID m_internalId;	// ID of internal object in the resource
		CName m_name;							// node name
		CName m_kind;							// kind of the object
	};

} // tools

RTTI_CUSTOM_TYPE_NAME( tools::EditorObjectID, "EditorObjectID" );
