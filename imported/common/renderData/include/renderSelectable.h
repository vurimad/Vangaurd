/*
 * Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "renderDataApi.h"

#include "../../redReflection/include/editorObjectIdPath.h"

/// Something selectable in rendering
/// NOTE: kind of temporary due to high cost
class RENDER_DATA_API IRenderSelectable
{
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
public:
	IRenderSelectable();
	virtual ~IRenderSelectable();

	typedef Uint32 GlobalID;

	/// Create selection editor object ID that represents a reference to the selected object
	virtual tools::EditorObjectIDPath Selectable_CreateEditorId() const = 0;

	/// If returns true, the object will have extended selection range.
	/// It's useful for hard to select objects (lines or points that are one pixel wide).
	virtual Bool Selectable_HasExtendedRange() const;

	/// Get the internal global ID
	GlobalID Selectable_GetID() const;

	/// Encode ID as color
	static Color EncodeID( const GlobalID id );

	/// Decode ID from color
	static IRenderSelectable::GlobalID DecodeID( const Color& color );

	/// Find selection object by ID
	/// NOTE: assumes that the selectable won't be freed
	static tools::EditorObjectIDPath ResolveSelectionID( const GlobalID id, Bool extendedRangeOnly );

private:
	static IRenderSelectable* HelperFindByID( const GlobalID id );

#ifndef NO_EDITOR

	/// internal ID
	GlobalID		m_id;

	/// list of all selectable object
	static red::HashMap< GlobalID, IRenderSelectable* >		st_list;
	static red::SpinLock									st_listMutex;

#endif

	void Link();
	void Unlink();
};

