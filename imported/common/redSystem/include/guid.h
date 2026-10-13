/**
* Copyright (c) 2013-16 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_GUID_H_
#define _RED_GUID_H_

#include "redSystemPublic.h"
#include "hash.h"
#include "typetraits.h"

// Parts to a Guid (Number of Uint32s)
#define RED_GUID_NUM_PARTS			( 4 )

// Number of things that go between parts when displayed as a string. Always 1 less than the number of parts
#define RED_GUID_NUM_DIVIDERS		( RED_GUID_NUM_PARTS - 1 )

// PrintF formatting for the guid when it's converted to and from a string
#define RED_GUID_STRING_FORMAT		( "%08X-%08X-%08X-%08X" )

// Includes the null terminator for char buffer sizes
#define RED_GUID_STRING_BUFFER_SIZE	( sizeof( Uint32 ) * 2 * RED_GUID_NUM_PARTS + RED_GUID_NUM_DIVIDERS + 1 )

namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// GUID - Global Unique ID
	class REDSYSTEM_API GUID
	{
	public:
		static const GUID ZERO;		// Empty GUID

	public:
		union
		{
			struct
			{
				Uint32	A,B,C,D;		//!< Guid numbers
			} parts;
			Uint32 guid[ RED_GUID_NUM_PARTS ];
		};

	public:
		// Default constructor
		RED_FORCE_INLINE GUID()
		{
			red::Memzero( this, sizeof( GUID ) );
		}

		// Initialization constructor
		RED_FORCE_INLINE explicit GUID( Uint32 a, Uint32 b, Uint32 c, Uint32 d )
		{
			parts.A = a;
			parts.B = b;
			parts.C = c;
			parts.D = d;
		}

		//
		explicit GUID( const char* str );

		//
		Bool FromString( const char* str );

		//
		void ToString( char* buffer, Uint32 bufferSize, const char* customFormat = nullptr ) const;

		RED_INLINE GUID& operator=( const GUID& other )
		{
			parts.A = other.parts.A;
			parts.B = other.parts.B;
			parts.C = other.parts.C;
			parts.D = other.parts.D;
			return *this;
		}

		// Compare GUIDs
		RED_INLINE Bool operator==( const GUID& other ) const
		{
			return ( parts.A == other.parts.A ) && ( parts.B == other.parts.B ) && ( parts.C == other.parts.C ) && ( parts.D == other.parts.D );
		}

		// Compare GUIDs
		RED_INLINE Bool operator!=( const GUID& other ) const
		{
			return !( *this == other );
		}

		// Is the GUID zero ?
		RED_INLINE Bool IsZero() const
		{
			return !parts.A && !parts.B && !parts.C && !parts.D;
		}

		// Starting with A as most significant and D as least
		RED_INLINE Bool operator<( const GUID& other ) const
		{
			if( parts.A < other.parts.A )
				return true;
			if( parts.A > other.parts.A )
				return false;

			// Integers are equal, move on to next most significant part
			if( parts.B < other.parts.B )
				return true;
			if( parts.B > other.parts.B )
				return false;

			// Integers are equal, move on to next most significant part
			if( parts.C < other.parts.C )
				return true;
			if( parts.C > other.parts.C )
				return false;

			// Integers are equal, move on to next most significant part
			if( parts.D < other.parts.D )
				return true;

			return false;
		}

		RED_FORCE_INLINE Uint32 CalcHash() const
		{
			return parts.A ^ parts.B ^ parts.C ^ parts.D;
		}

	public:
		// Create GUID
		static GUID Create();
		static GUID Create( const char* str );
	};

	// Helper class used to generate runtime (gameplay only / savegame'able) GUIDs; only to be used by game savegame system
	class REDSYSTEM_API CRuntimeGUIDGenerator
	{
		friend class GUID;
	public:
		// Gets runtime counter value; to be used when making savegame
		static Uint64 GetRuntimeCounter();
		// Sets runtime counter value; to be used when loading savegame
		static void SetRuntimeCounter( Uint64 counter );
	private:
		// Verifies if given non-tuntime GUID is valid; can be used to verify GUIDs of editor data won't collide with runtime data
		static Bool CanCollideWithRuntimeGUID( const GUID& guid );
		// Generates new runtime GUID (increases internal 64-bit counter)
		static GUID CreateGUID();
	};

}

RED_ALLOW_TYPE_AS_POD(red::GUID);

#endif // _RED_GUID_H_
