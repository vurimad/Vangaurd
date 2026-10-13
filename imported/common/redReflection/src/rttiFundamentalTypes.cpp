/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiFundamentalTypes.h"
#include "../../redFileSystem/include/file.h"
#include "rttiType.h"
#include "rttiSystem.h"
#include "rttiRegistration.h"
#include "rttiAccessPath.h"
#include "rttiValueHolder.h"
#include "rttiSingleValueHolder.h"
#include "rttiUtils.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

/// NOTE: the goal is to hide dirty type stuff in cpp files if possible

namespace rtti
{
	/// Fundamental type definition - the fundamental types have built-in ToString/FromString and serialization functions
	template < class T >
	class IFundamentalType : public rtti::IType
	{
	public:
		virtual ERTTITypeType GetType() const 
		{
			return RT_Fundamental; 
		}

		virtual Uint32 GetSize() const override final
		{
			return sizeof(T);
		}

		virtual Uint32 GetAlignment() const override final
		{
			return __alignof(T);
		}

		virtual void Construct( void* object ) const override final
		{
			// empty
		}

		virtual void Destruct( void* /*object*/ ) const override final
		{
			// empty
		}

		virtual Bool Compare( const void* data1, const void* data2, Uint32 /*flags*/ ) const override final
		{
			return *( const T* ) data1 == *( const T*) data2;
		}

		virtual void Copy( void* dest, const void* src ) const override final
		{
			*( T* ) dest = *( const T* ) src;		
		}

		virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
		{
			file << (*(T*) data);
			return true;
		}

		virtual Bool NeedsCleaning() const override final
		{		
			return false;
		}

		virtual Bool IsTriviallyMovable() const override final 
		{		
			return true;
		}

		virtual Bool ToString( const void* data, String& valueString ) const override
		{
			return ::ToString( valueString, *( const T*) data );
		}

		virtual Bool FromString( void* data, const String& valueString ) const override
		{
			return ::FromString( valueString, *(T*) data );
		}

		virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override
		{
			if ( !path.IsEmpty() )
				return ctx.ReportError( this, "Simple type does not has fine structure" );

			String text;
			if ( !::ToString( text, *( const T*) data ) )
				return ctx.ReportError( this, "Unable to convert value to text" );

			outValue = rtti::ValueHolder::CreateSingle( text.AsChar() );
			return true;
		}

		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override
		{
			if ( !path.IsEmpty() )
				return ctx.ReportError( this, "Simple type does not has fine structure" );

			if ( newValue.IsEmpty() )
			{
				*(T*)data = T();
				return true;
			}
			else if ( newValue.IsSingle() )
			{
				const auto& valueString = newValue.GetSingle()->ToString();
				if ( ::FromString( valueString, *(T*) data ) )
					return true;

				return ctx.ReportError( this, "Unable to restore value from text" );
			}
			else
			{
				return ctx.ReportError( this, "Simple types do not support fine structured values" );
			}
		}
	};

#define RTTI_IMPLEMENT_FUNDAMENTAL_TYPE(_type)									\
class FundamentalType##_type : public rtti::IFundamentalType<_type>				\
{																				\
	virtual const CName GetName() const { return GetTypeName<_type>(); }		\
	virtual CName GetRefName() const											\
	{																			\
		return rtti::FormatScriptedReferenceTypeName( GetTypeName<_type>() );	\
	}																			\
};

#define RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, _type )							\
{																					\
	RTTIRegistrator registrator( [ &typeSystem ]() {								\
		TypeHash nativeHash = GetNativeTypeHash<_type>();							\
		typeSystem.RegisterType( RED_NEW( FundamentalType##_type ), nativeHash );	\
	} );																			\
}

RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Bool );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Uint8 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Int8 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Uint16 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Int16 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Uint32 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Int32 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Float );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Double );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Int64 );
RTTI_IMPLEMENT_FUNDAMENTAL_TYPE( Uint64 );

	void RegisterFundamentalTypes( ITypeSystem& typeSystem )
	{
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Bool );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Uint8 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Int8 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Uint16 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Int16 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Uint32 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Int32 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Float );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Double );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Int64 );
		RTTI_REGISTER_FUNDAMENTAL_TYPE( typeSystem, Uint64 );
	}
}