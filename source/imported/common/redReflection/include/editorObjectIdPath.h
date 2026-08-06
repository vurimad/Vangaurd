/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editorObjectId.h"

namespace tools
{

	/// EditorObjectID "breadcrumbs" - used to address a nested object - i.e. a node inside a prefab
	class RED_REFLECTION_API EditorObjectIDPath
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_TYPE(EditorObjectIDPath);

	public:
		// empty path
		EditorObjectIDPath();

		EditorObjectIDPath( EditorObjectIDPath&& other ) = default;
		EditorObjectIDPath& operator=( EditorObjectIDPath&& other ) = default;
		EditorObjectIDPath( const EditorObjectIDPath& other ) = default;
		EditorObjectIDPath& operator=( const EditorObjectIDPath& other ) = default;

		// copy only specified number of elements
		explicit EditorObjectIDPath( const EditorObjectIDPath& other, Uint32 numElements );

		// construct a root path
		explicit EditorObjectIDPath(const EditorObjectID& root);

		// construct with given a list of IDs
		explicit EditorObjectIDPath( const red::ArraySpan< const EditorObjectID >& elements );
		explicit EditorObjectIDPath( red::DynArray< EditorObjectID >&& elements );

		// construct a child path
		explicit EditorObjectIDPath( const EditorObjectIDPath& parent, const EditorObjectID& child );
		explicit EditorObjectIDPath( const EditorObjectID& parent, const EditorObjectIDPath& child );
		explicit EditorObjectIDPath( const EditorObjectIDPath& parent, const EditorObjectIDPath& child );
		explicit EditorObjectIDPath( EditorObjectIDPath&& parent, const EditorObjectID& child );

		//--

		// compare
		Bool operator == ( const EditorObjectIDPath& other ) const;

		// compare
		Bool operator != ( const EditorObjectIDPath& other ) const;

		// less - maps/set
		Bool operator < ( const EditorObjectIDPath& other ) const;

		// remove elements from the end
		void RemoveLastElements( Uint32 num );

		// return new path with removed elements from the end
		EditorObjectIDPath RemovedLastElements( Uint32 num ) const;

		// returns true if the path is non-empty and all the elements are valid
		Bool IsValid() const;

		// is the path empty ?
		Bool IsEmpty() const;

		// get addressable object elements
		typedef red::DynArray<EditorObjectID> TPathElements;
		const TPathElements& GetElements() const;

		// get depth of the path
		Uint32 GetDepth() const;

		const EditorObjectID& GetFirstChildID() const;

		// get the final child id
		const EditorObjectID& GetFinalChildID() const;

		// Remove first element on the path, making it a sub-path.
		void RemoveFirstElement();

		// return path that does not contain elements of a given kind
		EditorObjectIDPath StrippedFromKind( CName kind ) const;

		// same as StrippedFromKind, but last element is always preserved
		EditorObjectIDPath StrippedFromIntermediateKind( CName kind ) const;

		// same as StrippedFromIntermediateKind, but last n elements of kind are preserved
		EditorObjectIDPath StrippedFromKindConstSufix( CName kind ) const;

		// check if two paths has common root
		static Bool HasCommonParent( const EditorObjectIDPath& a, const EditorObjectIDPath& b );

		// check if path a lies inside path b
		static Bool StartsWith( const EditorObjectIDPath& a, const EditorObjectIDPath& b );

		String ToString() const;	
		static Bool FromString( const String& str, EditorObjectIDPath& outIdPath );

		Uint32 CalcHash() const;

		Uint64 CalcHash64() const;

	private:
		TPathElements m_elements;
	};

} // tools
