/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redReflection/include/types.h"
#include "instanceDataLayoutCompiler.h"
#include "../../redCore/include/redCorePublic.h"
namespace rtti
{
	class IType;
}

/// Container for instance variable
class RED_CORE_API InstanceVar
{
	friend class InstanceDataLayout;
	friend class InstanceDataLayoutCompiler;

	static Uint32 MAX_OFFSET;

protected:
	Uint32			m_offset;		//!< Offset in the instance buffer
	Uint16			m_numElem;		//!< Number of variable occurences (in most cases: 1)
	INST_BUFFER_DEBUG( Uint16 m_index; )	//!< Index in the instance buffer
	const rtti::IType*		m_type;			//!< Type of the data ( used to serialize, destroy, etc )

protected:
	RED_INLINE void SetOffset( Uint32 offset ) { m_offset = offset; }
public:
	INST_BUFFER_DEBUG( RED_INLINE void SetIndex( Uint32 index ) { m_index = Uint16( index ); } )

	//! Get offset in the instance buffer
	RED_INLINE Uint32 GetOffset() const { return m_offset; }

	//! Get type of data
	RED_INLINE const rtti::IType* GetType() const { return m_type; }

	//Returns num of elements
	RED_INLINE Uint32 GetNumElem() const { return m_numElem; }

	//! Is this a valid variable ?
	RED_INLINE Bool IsValid() const { return m_type != NULL; }

	//! Is this variable instanced in current data layout ?
	RED_INLINE Bool IsInstanced() const { return m_offset != MAX_OFFSET; }

	friend InstanceDataLayoutCompiler& operator<<( InstanceDataLayoutCompiler& comp, InstanceVar& inst )
	{
		Uint32 offset = comp.AddVar( inst );
#if defined(NO_EDITOR)
		inst.SetOffset( offset );
#endif
		return comp;
	}

	//! Runtime test if given type is supported
	Bool SupportsType( const rtti::IType* type ) const { return m_type && rtti::CanCast( m_type, type ); };

	template < class T >
	Bool SupportsType() const { return SupportsType( ::GetTypeObject< T >() ); }

public:
	//! Default constructor, needs later initialization
	InstanceVar();

	//! Constructor, assign type to instance var
	InstanceVar( CName typeName );

	InstanceVar( const rtti::IType* type );

	//! Initialization needed when used with default constructor
	void Initialize( const rtti::IType* type );
};

template< class T >
class TInstanceVarArray : public InstanceVar
{
public:
	//! Constructor
	TInstanceVarArray()
		: InstanceVar( GetTypeName< T >() )
	{};	

	struct InstanceVarArrayCompilerProxy
	{
		InstanceVarArrayCompilerProxy( TInstanceVarArray<T>* var, Uint32 numElem )
			: m_var( var), m_numElem( numElem )
		{}
		Uint32 m_numElem;
		TInstanceVarArray<T>* m_var;
	};

	virtual void OnCompileLayout( InstanceDataLayoutCompiler& comp ) {}

	InstanceVarArrayCompilerProxy operator[]( Uint32 numElem ) { return InstanceVarArrayCompilerProxy( this, numElem ); }

	friend InstanceDataLayoutCompiler& operator<<( InstanceDataLayoutCompiler& comp, InstanceVarArrayCompilerProxy inst )
	{
		Uint32 offset = comp.AddVar( *inst.m_var, inst.m_numElem );	
#if defined(NO_EDITOR)
		inst.m_var->SetOffset( offset );
#endif
		inst.m_var->OnCompileLayout( comp );
		return comp;
	}

private:
	//! Post construction initialization is non accessible in that use case
	void Initialize( const rtti::IType* type ) {}
};

/// Template version of instance data
 template< class T >
 class TInstanceVar : public InstanceVar
 {
 public:
 	//! Constructor
	RED_INLINE TInstanceVar()
		: InstanceVar( GetTypeObject<T>() )
 		//: InstanceVar( GetTypeName<T>() )
 	{};
 private:
	//! Post construction initialization is non accessible in that use case
	 void Initialize( const rtti::IType* type ) {}
 };

 //Variable size version of TInstanceVarArray. Requires initialization before first use; use Init()
 template< class T >
 class TInstanceVarArrayExt : public TInstanceVarArray<T>
 {
	 TInstanceVar< Uint32 > m_arraySize;

	 virtual void OnCompileLayout( InstanceDataLayoutCompiler& comp ) override { comp << m_arraySize; }

 public:
	 Uint32 GetSizeVarOffset() const { return m_arraySize.GetOffset(); }
 private:
	 //! Post construction initialization is non accessible in that use case
	 void Initialize( const rtti::IType* type ) {}
 };

 // FIXME: Temporary workaround for x64.
 // Won't be a fix for deserializing with
 // same data on x64 and x86.
 typedef char* TGenericPtr;
