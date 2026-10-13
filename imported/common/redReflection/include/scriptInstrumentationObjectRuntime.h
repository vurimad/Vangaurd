/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiFunction.h"
#include "../../redCore/include/instrumentationObject.h"

#ifdef USE_PROFILER

namespace script
{
	static const Int32 s_invalidCodeOffset = -1;

	class RuntimeInstrumentationObject
	{
	public:
		RuntimeInstrumentationObject( Int32 startProfileCodeOffset, rtti::Function* function );
		RuntimeInstrumentationObject();
		~RuntimeInstrumentationObject();

		void Clear();

	private:
		const Uint8* GetInstrumentationObjectByte( Int32 codeOffset ) const;
		RED_INLINE Uint8* GetInstrumentationObjectByte( Int32 codeOffset ) { return const_cast< Uint8* >( static_cast< const RuntimeInstrumentationObject* >( this )->GetInstrumentationObjectByte( codeOffset ) ); }
		red::InstrumentationObject* GetInstrumentationObject( const Uint8* instrObjByte );

		Uint8* SetScopeName( Int32 codeOffset );
		Uint8* GetScopeNameByte( Int32 codeOffset ) const;
		Uint8* SkipScopeNameBytes( Uint8* data ) const;

		Uint8* CopyScopeName( Uint8* data );

		Int32 m_startProfileCodeOffset;
		rtti::Function* m_function;
		static const Uint32 c_maxScopeNameLength = RED_KILO_BYTE( 1 );
		char m_scopeName[ c_maxScopeNameLength ];
		RED_ALIGN( alignof( red::InstrumentationObject ) ) char m_instrObj[ sizeof( red::InstrumentationObject ) ];
	};
}

#endif
