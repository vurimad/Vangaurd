/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "debugFilterMask.h"

namespace rend
{
	/// a debug filter option, controls what is rendered in to the debug view (like old SHOW_Flags did)
	class RENDER_DATA_API DebugFilter
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		DebugFilter( const AnsiChar* path );
		DebugFilter( const Uint16 bitIndex, const DebugFilterMask& mask );
		/// get the mask assigned to this option (just the option mask)
		RED_FORCE_INLINE const Uint16 GetBitIndex() const { return m_bitIndex; }

		/// get the mask assigned to whole chain (bit + parent bits). All bits must be on to render stuff.
		RED_FORCE_INLINE const DebugFilterMask& GetTestMask() const { return m_testMask; }

		/// toggle default filter on/on based on the filter name, slow
		static void ToggleFilter( DebugFilterMask& mask, const AnsiChar* path, const Bool value );

		static void ToggleFilter( DebugFilterMask& mask, const DebugFilter& df, const Bool value );

	private:
		DebugFilter();

		Uint16					m_bitIndex;	// index of the owned bit
		DebugFilterMask			m_testMask;	// mask with parents
	};

	/// hierarchy of the debug filters (show flags)
	class RENDER_DATA_API DebugFilterHierarchyNode
	{
		RED_USE_MEMORY_POOL( red::PoolDebug );

	public:
		///! get the root element
		static DebugFilterHierarchyNode& GetRoot();

		//! get children elements
		typedef red::StaticArray< const DebugFilterHierarchyNode*, 256 > TChildren;
		void GetChildren( TChildren& outChildren ) const;

		//! get parent filter
		RED_FORCE_INLINE const DebugFilterHierarchyNode* GetParent() const { return m_parent; }

		//! get name of the element
		RED_FORCE_INLINE const AnsiChar* GetName() const { return m_name.AsChar(); }

		//! do we have children ?
		RED_FORCE_INLINE const Bool HasChildren() const { return m_children != nullptr; }

		//! do we have a parent ?
		RED_FORCE_INLINE const Bool HasParent() const { return m_parent != nullptr; }

		//! get the allocated bit
		RED_FORCE_INLINE const Uint16 GetBitIndex() const { return m_bitIndex; }

		//! create the requested filter node, a new node will be created if it does not exist
		static const DebugFilterHierarchyNode* CreateNode( const AnsiChar* path );

		static void RemoveNode(const AnsiChar* path);

		//! find debug filter by path
		static const DebugFilterHierarchyNode* FindNode( const AnsiChar* path );

		//! create a debug filter from this node
		DebugFilter GetFilter() const;

		//! get the testing bit mask
		void ComputeTestMask( DebugFilterMask& mask ) const;

		// Get full path, ie parents names also
		String GetFullPath() const;

	private:
		DebugFilterHierarchyNode( DebugFilterHierarchyNode* parent, const AnsiChar* name );

		Uint16						m_bitIndex;

		String						m_name;
		DebugFilterHierarchyNode*	m_parent;
		DebugFilterHierarchyNode*	m_children;
		DebugFilterHierarchyNode*	m_next;

		static red::SpinLock		st_lock;

		const DebugFilterHierarchyNode* FindChild( const AnsiChar* name ) const;

		DebugFilterHierarchyNode* CreateChild( const AnsiChar* name );

		static const Uint16 AllocBitIndex();
	};

} // rend
 