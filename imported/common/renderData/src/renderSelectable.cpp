/**
* Copyright (c) 2009 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderSelectable.h"

#ifndef NO_EDITOR
red::Atomic< Uint32 >		GSelectableIDAllocator(1);

red::HashMap< IRenderSelectable::GlobalID, IRenderSelectable* >	IRenderSelectable::st_list { rend::PoolImageProcessing() };
red::SpinLock													IRenderSelectable::st_listMutex;
#endif

// Only 24 bits should be used for id, to avoid using alpha channel
IRenderSelectable::IRenderSelectable()
#ifndef NO_EDITOR
	: m_id( GSelectableIDAllocator.Increment() & 0x00FFFFFF )
#endif
{
#ifndef NO_EDITOR
	Link();
#endif
}

IRenderSelectable::~IRenderSelectable()
{
#ifndef NO_EDITOR
	Unlink();
#endif
}

Bool IRenderSelectable::Selectable_HasExtendedRange() const
{
	return false;
}

Color IRenderSelectable::EncodeID( const GlobalID id )
{
	return Color( (Uint8)(id & 0xFF), (Uint8)((id  >> 8) & 0xFF), (Uint8)(( id >> 16 ) & 0xFF), (Uint8)(( id >> 24 ) & 0xFF) );
}

IRenderSelectable::GlobalID IRenderSelectable::DecodeID( const Color& color )
{
	IRenderSelectable::GlobalID ret = 0;
	ret |= ((Uint32)color.R) << 0;
	ret |= ((Uint32)color.G) << 8;
	ret |= ((Uint32)color.B) << 16;
	//ret |= ((Uint32)color.A) << 24;
	return ret;
}

tools::EditorObjectIDPath IRenderSelectable::ResolveSelectionID( const GlobalID id, Bool extendedRangeOnly )
{
#ifndef NO_EDITOR
	if ( id == 0 )
		return tools::EditorObjectIDPath();

	{
		red::ScopedLock< red::SpinLock > lock( st_listMutex );
		IRenderSelectable* ptr = HelperFindByID( id );
		if ( ptr )
		{
			if ( !extendedRangeOnly || ptr->Selectable_HasExtendedRange() )
			{
				return ptr->Selectable_CreateEditorId();
			}
		}
	}
#endif

	// nothing found
	return tools::EditorObjectIDPath();
}

IRenderSelectable* IRenderSelectable::HelperFindByID( const GlobalID id )
{
#ifndef NO_EDITOR
	IRenderSelectable* retPtr = nullptr;
	st_list.Find( id, retPtr );
	return retPtr;
#else 
	return nullptr;
#endif
}

void IRenderSelectable::Link()
{
#ifndef NO_EDITOR
	red::ScopedLock< red::SpinLock > lock( st_listMutex );
	st_list.Insert( m_id, this );
#endif
}

void IRenderSelectable::Unlink()
{
#ifndef NO_EDITOR
	red::ScopedLock< red::SpinLock > lock( st_listMutex );
	st_list.Remove( m_id );
#endif
}

IRenderSelectable::GlobalID IRenderSelectable::Selectable_GetID() const 
{
#ifndef NO_EDITOR
	return m_id; 
#else
	return 0;
#endif

}