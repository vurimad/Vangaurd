/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "string.h"
#include "mathVector4.h"
#include "mathQuaternion.h"
#include "serializable.h"



namespace mathExpr
{
	enum VarType
	{
		VarInvalid = 0,
		VarScalar = 1,
		VarVector = 2,
		VarRotation = 3,
		VarSize
	};

	struct Token
	{
		Token( Uint32 val = 0 ) : m_data( val ) {}

		RED_INLINE void			SetValIndex( Uint16 val )	{ m_data = val | ( m_data & 0xFFFF0000 ); }		
		RED_INLINE void  		SetType( VarType val )	{ m_data = Uint32(val) << 24  | ( m_data & 0xF0FFFFFF ); }
		RED_INLINE void 		SetNumOfArgs( Uint8 vals )	{ m_data = ( vals << 28 ) | ( m_data & 0x8FFFFFFF ); }
		RED_INLINE void 		SetIsOperator( Bool val )	{ m_data ^= ( -Int32(val) ^ m_data ) & RED_FLAG( 31 ); }

		RED_INLINE Uint16		ValIndex() const { return ( m_data & 0x0000FFFF ); }					// Data index
		RED_INLINE VarType  	Type() const	{ return VarType( ( m_data & 0x0F000000 ) >> 24 ); } 	// Type of operand and return type of a function
		RED_INLINE Uint8 		NumOfArgs()	const { return ( m_data & 0x70000000 ) >> 28; }				// Number of arg for operators				 			
		RED_INLINE Bool 		IsOperator() const { return ( m_data & RED_FLAG( 31 ) ) != 0; }				

		Uint32 m_data;
	};

	class Expression;

	enum AllowedOperations
	{
		ScalarOperations = RED_FLAG(1),
		VectorOperations = RED_FLAG(2),
		RotationOperations = RED_FLAG(3),
		LogicalOperations = RED_FLAG(4)
	};

	enum OperatorType // Because of packing it needs to be stored on 2 bits
	{
		GlobalOp = 0,
		ProjectGlobalOp = 1,
		UserRegisteredOp = 2,
	};

	struct RED_REFLECTION_API ParsingContext
	{
	public:
		template< typename T> void RegisterConstant( const String& name, const T& arg );

		Uint32 RegisterVar( const String& name, VarType type );		

		void SetExpression( const String& name );
		void AllowAutoRegisteringVars( Bool val ) { m_autoRegisterVars = val; }
		void SetAllowedOperations( Uint32 op ) { m_allowedOperations = op; }
		void SetReturnedType( VarType type )  { m_forceReturnType = type; }

		Bool IterateUsedVars( Uint32& lastVarIndex, String& outName, VarType& outType, Uint32& outID );

		ParsingContext();

		struct RegisteredVar
		{								
			String			name;				
			Uint32				varId;
			VarType				type;
			Bool				isConst	;
		};

		String	m_expression;
		Uint32		m_curChar;
		Uint32		m_allowedOperations;
		Bool		m_autoRegisterVars;
		VarType		m_forceReturnType;

		red::DynArray<Float> m_constValData{ red::PoolEngine() };

		//Registered stuff
		red::DynArray< RegisteredVar >		m_registeredVars{ red::PoolEngine() };
		red::DynArray< RegisteredVar >		m_registeredConst{ red::PoolEngine() };

		//Parsing data
		red::DynArray<Token>		m_tokenData{ red::PoolEngine() };
		red::DynArray<Float>		m_valuesData{ red::PoolEngine() };
		red::DynArray<String>	m_tempData{ red::PoolEngine() };

		String					m_errorMessage;

		Bool (*m_projectGlobalOpBinder)( const String& op, VarType& outReturnType, Uint16& outId, Uint32 allowedOp, VarType a1, VarType a2, VarType a3, VarType a4 );
	};

	RED_REFLECTION_API Bool CreateExpression( ParsingContext& input, Expression& out );
	
	struct RED_REFLECTION_API ExecutionContext
	{

		ExecutionContext();

		struct RegisteredVar
		{
			Uint32 id;
			Uint32 index : 16;
			VarType type : 16;
		};

		VarType									remainingVarType;

		red::StaticArray< Float, 32 >			data;
		red::StaticArray< RegisteredVar, 16 >	idToIndex;
		red::StaticArray< Float, 32 >			varData;

		Bool (*m_projectGlobalOpExecutor)( Uint16 id, ExecutionContext& context );

		template< typename T > void RegisterVarValue( Uint32 varId, const T& val );
		VarType GetRemainingVarType() const { return remainingVarType; }

		Bool UseRegisteredVar( Uint32 operatorId );
		Float GetFloat() { return data.PopBack(); }
		Vector4 GetVector();
		Quaternion GetQuaternion();

		void ReturnFloat( Float arg ) { remainingVarType = VarScalar; data.PushBack( arg ); }
		void ReturnVector( const Vector4& arg );			
		void ReturnQuaternion( const Quaternion& arg );
	};

	class RED_REFLECTION_API Expression : public ISerializable
	{
		RTTI_DECLARE_TYPE( Expression )
		RED_USE_MEMORY_POOL( red::PoolEngine );

		RED_REFLECTION_API friend Bool CreateExpression( ParsingContext&, Expression& );
	public:		
		Bool Calculate( ExecutionContext& result ) const;		
		VarType GetReturnType() const { return VarType(m_returnVarType); }

	private:
		red::DynArray<Uint32>	m_tokenData{ red::PoolEngine() };
		red::DynArray<Float>	m_valuesData{ red::PoolEngine() };
		Uint16					m_returnVarType;
	};

	Uint32 RED_FORCE_INLINE GetTypeSize( VarType type )
	{
		switch( type )
		{
		case VarScalar :
			return 1;
		case VarVector :
		case VarRotation :
			return 4;
		default:
			return 0;
		}
	}

	namespace ExpressionToolkit
	{
		template< typename SrcArr, typename DstArr >
		void CopyVarAcrossBuffers( DstArr& dest, SrcArr& src, Uint32 srcInd, VarType type )
		{	
			Uint32 typeSize = GetTypeSize( type );
			Uint32 destInd = dest.Size();
			red::ArrayImplUtils::GrowNoConstruct( dest, typeSize );						
			for( Uint32 i = 0; i < typeSize; ++i )
			{
				dest[ destInd + i ] = src[ srcInd + i ];				
			}
		}
	}

	template< typename T > RED_FORCE_INLINE VarType TranslateType();
	template<> RED_FORCE_INLINE VarType TranslateType<Vector4>(){ return VarVector; }
	template<> RED_FORCE_INLINE VarType TranslateType<Float>(){ return VarScalar; }
	template<> RED_FORCE_INLINE VarType TranslateType<Quaternion>(){ return VarRotation; }

	template< typename T > RED_FORCE_INLINE const Float* ToDataPtr( const T& data );
	template<> RED_FORCE_INLINE const Float* ToDataPtr<Vector4>( const Vector4& data ){ return data.AsFloat(); }
	template<> RED_FORCE_INLINE const Float* ToDataPtr<Float>( const Float& data ){ return &data; }
	template<> RED_FORCE_INLINE const Float* ToDataPtr<Quaternion>( const Quaternion& data ){ return &data.i; }

	template< typename T>
	void ParsingContext::RegisterConstant( const String& name, const T& arg )
	{
		auto var = std::find_if( m_registeredVars.Begin(), m_registeredVars.End(), [ name ]( const ParsingContext::RegisteredVar & var ){ return var.name == name; } ); 
		if( var == m_registeredVars.End() )
		{
			m_registeredVars.Grow(1);	
			var = m_registeredVars.End() - 1;
		}
		Uint32 oldSize = m_constValData.Size();
		VarType type = TranslateType<T>();
		RegisteredVar newVar = { name, m_constValData.Size(), type, true };
		*var = newVar;
		const Float* data = ToDataPtr( arg );
		ExpressionToolkit::CopyVarAcrossBuffers( m_constValData, data, 0, type );
	}

	template< typename T >
	void ExecutionContext::RegisterVarValue( Uint32 varId, const T& val )
	{
		VarType type = TranslateType<T>();
		ExecutionContext::RegisteredVar var = { varId, varData.Size(), type };
		idToIndex.PushBack( var );
		const Float* data = ToDataPtr( val );
		ExpressionToolkit::CopyVarAcrossBuffers( varData, data, 0, type );		
	}
}



//***********************************************************************************************************//