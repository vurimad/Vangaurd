/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugFilter.h"
#include "debugFilterMask.h"
#include "../../redReflection/include/stringParser.h"

namespace rend
{
	//-----

	DebugFilter::DebugFilter()
	{
		// NOT ALLOWED
		m_bitIndex = 0;
	}

	DebugFilter::DebugFilter( const AnsiChar* path )
	{
		// create a permanent entry in the filter hierarchy
		const auto* node = DebugFilterHierarchyNode::CreateNode( path );

		// cache data
		m_bitIndex = node->GetBitIndex();
		node->ComputeTestMask( m_testMask );
	}

	DebugFilter::DebugFilter( const Uint16 bitIndex, const DebugFilterMask& mask )
		: m_bitIndex( bitIndex )
		, m_testMask( mask )
	{
	}

	void DebugFilter::ToggleFilter( DebugFilterMask& mask, const AnsiChar* path, const Bool value )
	{
		const auto* node = DebugFilterHierarchyNode::CreateNode( path );
		RED_ASSERT( node != nullptr, "Invalid filter node" );

		if ( node != nullptr )
		{
			if ( value )
				mask |= node->GetBitIndex();
			else
				mask -= node->GetBitIndex();
		}
	}

	void DebugFilter::ToggleFilter( DebugFilterMask& mask, const DebugFilter& df, const Bool value )
	{
		if( value )
			mask |= df.GetBitIndex();
		else
			mask -= df.GetBitIndex();
	}

	//-----

	red::SpinLock DebugFilterHierarchyNode::st_lock;

	DebugFilter DebugFilterHierarchyNode::GetFilter() const
	{
		DebugFilterMask mask;
		ComputeTestMask( mask );

		return DebugFilter( m_bitIndex, mask );
	}

	DebugFilterHierarchyNode& DebugFilterHierarchyNode::GetRoot()
	{
		static DebugFilterHierarchyNode theRoot( nullptr, "Root" );
		return theRoot;
	}

	const Uint16 DebugFilterHierarchyNode::AllocBitIndex()
	{
		static red::Atomic< Uint32 > GBitCounter(1);
		const auto ret = GBitCounter.Increment();
		RED_ASSERT( ret < rend::c_maxDebugFilter, "To many debug filter bits allocated" );
		return static_cast< Uint16 >( ret );
	}

	DebugFilterHierarchyNode::DebugFilterHierarchyNode( DebugFilterHierarchyNode* parent, const AnsiChar* name )
		: m_parent( parent )
		, m_children( nullptr )
		, m_next( nullptr )
		, m_name( name )
		, m_bitIndex( 0 )
	{
		if ( m_parent )
			m_bitIndex = AllocBitIndex();
	}

	void DebugFilterHierarchyNode::GetChildren( TChildren& outChildren ) const
	{
		for ( const auto* cur = m_children; cur; cur = cur->m_next )
			outChildren.PushBack( cur );
	}

	String DebugFilterHierarchyNode::GetFullPath() const
	{
		red::StringBuilder<String> builder;
		builder.Append( GetName() );

		String token = "/";

		const rend::DebugFilterHierarchyNode* parent = GetParent();
		while ( parent && String( parent->GetName() ) != String( "Root" ) )
		{
			builder.Prepend( token );
			builder.Prepend( parent->GetName() );
			parent = parent->GetParent();
		}

		return builder.ToString();
	}

	void DebugFilterHierarchyNode::ComputeTestMask( DebugFilterMask& mask ) const
	{
		const auto* cur = this;
		while ( cur )
		{
			if ( cur->m_bitIndex )
			{
				mask |= cur->m_bitIndex;
			}

			cur = cur->m_parent;
		}
	}

	const DebugFilterHierarchyNode* DebugFilterHierarchyNode::FindChild( const AnsiChar* name ) const
	{
		red::ScopedLock< red::SpinLock > lock( st_lock );

		// find existing
		for ( auto* ptr = m_children; ptr; ptr = ptr->m_next )
		{
			if ( ptr->m_name == name )
			{
				return ptr;
			}
		}

		// no child found
		return nullptr;
	}
	
	DebugFilterHierarchyNode* DebugFilterHierarchyNode::CreateChild( const AnsiChar* name )
	{
		red::ScopedLock< red::SpinLock > lock( st_lock );

		// find existing
		for ( auto* ptr = m_children; ptr; ptr = ptr->m_next )
		{
			if ( ptr->m_name == name )
			{
				return ptr;
			}
		}

		// create new one
		auto* ptr = RED_NEW( DebugFilterHierarchyNode )( this, name );
		ptr->m_next = m_children;
		m_children = ptr;

		return ptr;
	}

	const DebugFilterHierarchyNode* DebugFilterHierarchyNode::CreateNode( const AnsiChar* path )
	{
		RED_FATAL_ASSERT( path != nullptr, "Invalid category" );
		RED_FATAL_ASSERT( *path != 0, "Invalid filter name" );
		
		DebugFilterHierarchyNode* parent = &GetRoot();

		const AnsiChar* str = path;
		while ( *str )
		{
			String categoryName;
			if ( !GParseToken( str, categoryName ) )
				return nullptr;

			parent = parent->CreateChild( categoryName.AsChar() );
			RED_FATAL_ASSERT( parent != nullptr, "Failed to follow the path" );

			GParseKeyword( str, "/" );
		}

		return parent;
	}

	const DebugFilterHierarchyNode* DebugFilterHierarchyNode::FindNode( const AnsiChar* path )
	{
		if ( !path || !*path ) 
			return nullptr;

		const DebugFilterHierarchyNode* parent = &GetRoot();

		const AnsiChar* str = path;
		while ( *str )
		{
			String categoryName;
			if ( !GParseToken( str, categoryName ) )
				return nullptr;

			parent = parent->FindChild( categoryName.AsChar() );
			if ( !parent )
				return nullptr;

			GParseKeyword( str, "/" );
		}

		return parent;
	}

	//-----

	void DebugFilterHierarchyNode::RemoveNode(const AnsiChar* path)
	{
		RED_FATAL_ASSERT(path != nullptr, "Invalid category");
		RED_FATAL_ASSERT(*path != 0, "Invalid filter name");

		auto* node = FindNode(path);

		TChildren nodes;
		nodes.EmplaceBack(node);
		
		auto prevCh = node->m_parent->m_children;
		for (auto* ch = node->m_parent->m_children; ch != nullptr; prevCh = ch, ch = ch->m_next)
		{
			if (ch->GetName() == node->GetName())
			{
				prevCh->m_next = ch->m_next;
			}
		}

		if (node->m_parent->m_children == node)
		{
			node->m_parent->m_children = node->m_next;
		}
		

		for (auto currentNode = 0u; currentNode < nodes.Size(); ++currentNode)
		{
			nodes[currentNode]->GetChildren(nodes);
		}
		
		for (auto& n : nodes)
		{
			RED_DELETE(n);
		}
		
	}

} // rend