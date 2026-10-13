/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace rend
{
	class DebugFilter;
	class DebugFilterHierarchyNode;

	const Uint32 c_maxDebugFilter = 2048;
	const Int32 c_maxPathLength = 512;

	/// mask for debug filter
	class RENDER_DATA_API DebugFilterMask
	{
	public:
		RED_FORCE_INLINE DebugFilterMask();

		// logical operations
	
		DebugFilterMask operator|( const DebugFilter& filter ) const;
		DebugFilterMask operator-( const DebugFilter& filter ) const;
		DebugFilterMask operator^( const DebugFilter& filter ) const;

		// logical operations, inplace
		DebugFilterMask& operator|=( const DebugFilter& filter );
		DebugFilterMask& operator-=( const DebugFilter& filter );
		DebugFilterMask& operator^=( const DebugFilter& filter );
		DebugFilterMask& operator|=( const DebugFilterMask &other );
		RED_FORCE_INLINE DebugFilterMask& operator|=( const Uint16 bitIndex );
		RED_FORCE_INLINE DebugFilterMask& operator-=( const Uint16 bitIndex );
		RED_FORCE_INLINE DebugFilterMask& operator^=( const Uint16 bitIndex );

		bool operator==( const DebugFilterMask & other ) const;
		bool operator!=( const DebugFilterMask & other ) const;

		// test a SINGLE bit
		const Bool GetSingleBitState( const DebugFilter& filter ) const;
		const Bool GetSingleBitState( Uint32 index ) const;
		void SetSingleBitState( Uint32 index, bool value );
	
		// uses the test mask of the debug filter
		const Bool Test( const DebugFilter& filter ) const;

		static DebugFilterMask& GetFilterMask( const String & context );

		static Uint32 GetFilterMaskMapSize();
		static void GetFilterMaskMapKeys( red::DynArray< String >& keys );
		static void GetFilterMaskMapValues( red::DynArray< rend::DebugFilterMask >& values );

		//Utility functions to help with interop
		static String ConvertToString( const rend::DebugFilterMask &inMask );
		static DebugFilterMask BuildFromString( const String &inString );
		static void SetFilterMaskFromHierarchyString( const String &maskName, const String &filters );
		static String FilterHierarchyAsString( const String &maskName );
		static void FilterHierarchyAsString( const String& maskName, String& outputMask );
	private:
		struct DebugFilterParameter
		{
			DebugFilterParameter(
				rend::DebugFilterMask& mask,
				String& result,
				const rend::DebugFilterHierarchyNode* node,
				const char* sourcePath,
				Bool isGameFilter )
				: mask( mask )
				, result( result )
				, node( node )
				, isGameFilter( isGameFilter )
				, currentPathPtr( path )
			{
				red::Memzero( path, c_maxPathLength );
				if ( sourcePath )
				{
					RED_FATAL_ASSERT( static_cast< Uint32 >( red::Strlen( sourcePath ) ) < c_maxPathLength, "Not enough space to store debug filter path" );
					red::Strcpy( path, sourcePath, c_maxPathLength );
				}
			}

			rend::DebugFilterMask& mask;
			String& result;
			const rend::DebugFilterHierarchyNode* node;
			char path[ c_maxPathLength ];
			Bool isGameFilter;
			char* currentPathPtr;
		};

		static void ProcessNodeRecursion( DebugFilterParameter& param );

		static void SerializeBit( DebugFilterParameter& param );

		static red::HashMap< String, rend::DebugFilterMask > sm_maskMap;
		static red::RWSpinLock sm_maskMapLock;

		red::BitSet64< c_maxDebugFilter >	m_bits;
	};

	/// get default rendering filters
	extern RENDER_DATA_API DebugFilterMask& GetDefaultEditorFilters();
	extern RENDER_DATA_API DebugFilterMask& GetDefaultGameFilters();
	extern RENDER_DATA_API const char* GetGameFilterName();

} // rend

#include "debugFilterMask.inl"
