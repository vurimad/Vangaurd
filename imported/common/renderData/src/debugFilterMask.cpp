/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debugFilter.h"
#include "debugFilterMask.h"
#include "../../redContainers/include/bitSet.h"

namespace
{
	const char* const c_gameDebugFilterName = "game";
}

namespace rend
{
	DebugFilterMask& GetDefaultEditorFilters()
	{
		static DebugFilterMask editorFilters;
		return editorFilters;
	}

	DebugFilterMask& GetDefaultGameFilters()
	{
		static DebugFilterMask gameFilters;
		return gameFilters;
	}

	const char* GetGameFilterName()
	{
		return c_gameDebugFilterName;
	}

	DebugFilterMask& DebugFilterMask::GetFilterMask( const String & context )
	{
		String lowerString = context;
		lowerString.ToLower();

		rend::DebugFilterMask* mask;
		{
			RED_SCOPE_SHARED_LOCK( sm_maskMapLock );
			mask = sm_maskMap.FindPtr( lowerString );
		}

		if ( !mask )
		{
			auto& defaultFilters = context == c_gameDebugFilterName ? GetDefaultGameFilters() : GetDefaultEditorFilters();
			RED_SCOPE_LOCK( sm_maskMapLock );
			mask = &sm_maskMap.Insert( lowerString, defaultFilters ).Iterator().Value();
		}

		RED_ASSERT( mask, "Failed to find or create debug filters for %s", context.AsChar() );
		return *mask;
	}

	Uint32 DebugFilterMask::GetFilterMaskMapSize()
	{
		RED_SCOPE_SHARED_LOCK( sm_maskMapLock );
		return sm_maskMap.Size();
	}

	void DebugFilterMask::GetFilterMaskMapKeys( red::DynArray< String >& keys )
	{
		RED_SCOPE_SHARED_LOCK( sm_maskMapLock );
		sm_maskMap.GetKeys( keys );
	}

	void DebugFilterMask::GetFilterMaskMapValues( red::DynArray< rend::DebugFilterMask >& values )
	{
		RED_SCOPE_SHARED_LOCK( sm_maskMapLock );
		sm_maskMap.GetValues( values );
	}

	DebugFilterMask DebugFilterMask::operator|( const DebugFilter& filter ) const
	{
		DebugFilterMask ret( *this );
		ret |= filter.GetTestMask();
		return ret;
	}

	DebugFilterMask DebugFilterMask::operator-( const DebugFilter& filter ) const
	{
		DebugFilterMask ret( *this );
		ret.m_bits.Clear( filter.GetBitIndex() );
		return ret;
	}

	DebugFilterMask DebugFilterMask::operator^( const DebugFilter& filter ) const
	{
		DebugFilterMask ret( *this );
		ret.m_bits.Toggle( filter.GetBitIndex() );
		return ret;
	}

	DebugFilterMask& DebugFilterMask::operator|=( const DebugFilter& filter )
	{
		m_bits.Set( filter.GetBitIndex() );
		return *this;
	}

	DebugFilterMask& DebugFilterMask::operator-=( const DebugFilter& filter )
	{
		m_bits.Clear( filter.GetBitIndex() );
		return *this;	
	}

	DebugFilterMask& DebugFilterMask::operator^=( const DebugFilter& filter )
	{
		m_bits.Toggle( filter.GetBitIndex() );
		return *this;
	}

	DebugFilterMask& DebugFilterMask::operator|=( const DebugFilterMask &other )
	{
		m_bits |= other.m_bits;
		return *this;
	}

	bool DebugFilterMask::operator==( const DebugFilterMask & other ) const
	{
		return m_bits == other.m_bits;
	}

	bool DebugFilterMask::operator!=( const DebugFilterMask & other ) const
	{
		return m_bits != other.m_bits;
	}

	const Bool DebugFilterMask::GetSingleBitState( const DebugFilter& filter ) const
	{
		return m_bits.Get( filter.GetBitIndex() );
	}

	const Bool DebugFilterMask::GetSingleBitState( Uint32 index ) const
	{
		return m_bits.Get( index );
	}

	void DebugFilterMask::SetSingleBitState( Uint32 index, bool value )
	{
		value ? m_bits.Set(index) : m_bits.Clear(index);
	}


	const Bool DebugFilterMask::Test( const DebugFilter& filter ) const
	{
		return m_bits.IsAllSet( filter.GetTestMask().m_bits );
	}

	String DebugFilterMask::ConvertToString( const rend::DebugFilterMask &inMask )
	{
		return inMask.m_bits.AsString();
	}

	DebugFilterMask DebugFilterMask::BuildFromString( const String &inString )
	{
		DebugFilterMask filterMask;
		filterMask.m_bits.BuildFromString( inString );
		return filterMask;
	}

	void DebugFilterMask::SetFilterMaskFromHierarchyString( const String &maskName, const String &processedHierarchy )
	{
		red::DynArray< String > filterPathsAndValues{ red::PoolDebug() };
		processedHierarchy.GetTokens( ':', false, filterPathsAndValues );
		rend::DebugFilterMask &mask = rend::DebugFilterMask::GetFilterMask( maskName );

		for (Uint32 i = 0; i < filterPathsAndValues.Size(); i++)
		{
			String path = filterPathsAndValues[i++];
			String valueString = filterPathsAndValues[i];

			Bool value = valueString == "T" ? true : false;

			rend::DebugFilter::ToggleFilter( mask, path.AsChar(), value );
		}
	}

	String DebugFilterMask::FilterHierarchyAsString( const String &maskName )
	{
		String result;
		rend::DebugFilterHierarchyNode &node = rend::DebugFilterHierarchyNode::GetRoot();
		rend::DebugFilterMask &mask = rend::DebugFilterMask::GetFilterMask( maskName );

		const Bool isGameFilter = maskName == c_gameDebugFilterName;

		rend::DebugFilterHierarchyNode::TChildren children;
		node.GetChildren( children );
		for ( Uint32 i = 0; i < children.Size(); i++ )
		{
			DebugFilterParameter param { mask, result, children[i], nullptr, isGameFilter };
			ProcessNodeRecursion( param );
		}
		return result;
	}

	void DebugFilterMask::FilterHierarchyAsString( const String& maskName, String& outputMask )
	{
		rend::DebugFilterHierarchyNode &node = rend::DebugFilterHierarchyNode::GetRoot();
		rend::DebugFilterMask &mask = rend::DebugFilterMask::GetFilterMask( maskName );

		const Bool isGameFilter = maskName == c_gameDebugFilterName;

		rend::DebugFilterHierarchyNode::TChildren children;
		node.GetChildren( children );
		for ( Uint32 i = 0; i < children.Size(); i++ )
		{
			DebugFilterParameter param{ mask, outputMask, children[i], nullptr, isGameFilter };
			ProcessNodeRecursion( param );
		}
	}

	void DebugFilterMask::ProcessNodeRecursion( DebugFilterParameter& param )
	{
		auto& path = param.path;
		auto& node = param.node;

		const Int32 nameLength = static_cast< Int32 >( red::Strlen( node->GetName() ) );
		const Int32 freeSpace = c_maxPathLength - static_cast< Int32 >( param.currentPathPtr - param.path );
		RED_FATAL_ASSERT( freeSpace > 0 && nameLength <= freeSpace, "Not enough space to store debug filter" );
		red::Strcat( param.currentPathPtr, node->GetName(), freeSpace );
		param.currentPathPtr += nameLength;

		if ( node->HasChildren() )
		{
			rend::DebugFilterHierarchyNode::TChildren children;
			node->GetChildren( children );
			
			SerializeBit( param );
			
			const Int32 freeSpace = c_maxPathLength - static_cast< Int32 >( param.currentPathPtr - param.path );
			RED_FATAL_ASSERT( freeSpace > 0 && 1 <= freeSpace, "Not enough space to store debug filter");
			red::Strcat( param.currentPathPtr, "/", freeSpace );
			param.currentPathPtr += 1;

			for ( Uint32 i = 0; i < children.Size(); i++ )
			{
				DebugFilterParameter childParam { param.mask, param.result, children[i], path, param.isGameFilter };
				ProcessNodeRecursion( childParam );
			}
		}
		else
		{
			SerializeBit( param );
		}
	}

	void DebugFilterMask::SerializeBit( DebugFilterParameter& param )
	{
		Bool maskResult = param.mask.GetSingleBitState( param.node->GetBitIndex() );
		auto& defaultFilters = param.isGameFilter ? rend::GetDefaultGameFilters() : rend::GetDefaultEditorFilters();
		Bool defaultResult = defaultFilters.GetSingleBitState( param.node->GetBitIndex() );
		if ( maskResult != defaultResult )
		{
			auto& result = param.result; 
			result += param.path;
			if ( maskResult )
			{
				result += ":T:";
			}
			else
			{
				result += ":F:";
			}
		}
	}

	red::HashMap< String, rend::DebugFilterMask > DebugFilterMask::sm_maskMap{ red::PoolDebug() };
	red::RWSpinLock DebugFilterMask::sm_maskMapLock;

} // rend

