/**
* Copyright (c) 2007-17 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiType.h"
#include "rttiFunctionContext.h"
#include "scriptCompiledCode.h"
#include "reflectionPool.h"

class IScriptable;
class CScriptStackFrame;

namespace rep
{
	class Function;
}

/// Function flags
enum EFunctionFlags
{
	FF_NativeFunction		= RED_FLAG( 0 ),		//!< Function is native ( implemented in C++ )
	FF_StaticFunction		= RED_FLAG( 1 ),		//!< Function is static
	FF_FinalFunction		= RED_FLAG( 2 ),		//!< Function is final and cannot be overridden in child classes
	FF_EventFunction		= RED_FLAG( 3 ),		//!< Function is special event function
	FF_ExecFunction			= RED_FLAG( 4 ),		//!< Function can be called from console
	FF_UndefinedBody		= RED_FLAG( 5 ),		//!< Function has no body (just a declaration)
	FF_TimerFunction		= RED_FLAG( 6 ),		//!< Function is a timer
	FF_PrivateFunction		= RED_FLAG( 7 ),		//!< Function is private
	FF_ProtectedFunction	= RED_FLAG( 8 ),		//!< Function is protected
	FF_PublicFunction		= RED_FLAG( 9 ),		//!< Function is public
	FF_Unused1				= RED_FLAG( 10 ),		//!< Unused flag 1
	FF_Unused2				= RED_FLAG( 11 ),		//!< Unused flag 2
	FF_Unused3				= RED_FLAG( 12 ),		//!< Unused flag 3
	FF_ConstFunction		= RED_FLAG( 13 ),		//!< Function is const (read-only)
	FF_QuestFunction		= RED_FLAG( 14 ),		//!< Function is called by quest node
	FF_ThreadSafeFunction	= RED_FLAG( 15 )		//!< Function is thread safe (can safely be called at *any* time)
};

namespace rtti
{

	class Function;
	class Property;

	// Custom function caller interface; for use with non-IScriptable based classes
	class IFunctionCaller
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolRTTIFunction );

	public:
		virtual ~IFunctionCaller() {}
		virtual void Call( void* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType ) const = 0;
	};

	// Implementation of IFunctionCaller for functions of specific C++ class
	template < class TYPE >
	class TFunctionCaller : public IFunctionCaller
	{
	public:
		typedef void ( TYPE::*TCustomNativeFunc )( CScriptStackFrame&, void*, const rtti::IType* );
		TFunctionCaller( TCustomNativeFunc function ) : m_function( function ) {}
		void Call( void* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType ) const override
		{
			( static_cast< TYPE* >( context )->*m_function )( stack, result, resultType );
		}
	private:
		TCustomNativeFunc m_function;
	};

	class RED_REFLECTION_API IFunctionCollector
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolRTTIFunction );

	public:
		virtual ~IFunctionCollector();
		// return true when collection should be proceed / false on break
		virtual Bool operator()( const Function* f ) = 0;
	};

	/// RTTI Function definition
	class RED_REFLECTION_API Function : red::NonCopyable
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolRTTIFunction );

	public:
		Function( CName name, CName familyName, Uint32 flags );
		Function( Function&& other );
		virtual ~Function();

		//! Get the function name
		RED_INLINE const CName GetName() const { return m_name; }

		//! Get function family name
		RED_INLINE const CName GetFamilyName() const { return m_familyName; }

		Uint64 GetFunctionHash() const;

		//! Get function flags
		RED_INLINE Uint32 GetFlags() const { return m_flags; }

		//! Get class this function is defined in
		virtual const rtti::ClassType* GetClass() const { return nullptr; }

		//! Get size of needed stack data
		RED_INLINE Uint32 GetStackSize() const { return m_size; }

		//! Is this a native function ?
		RED_INLINE Bool IsNative() const { return 0 != ( m_flags & FF_NativeFunction ); }

		//! Is this a static function ?
		RED_INLINE Bool IsStatic() const { return 0 != ( m_flags & FF_StaticFunction ); }

		//! Is this final function ?
		RED_INLINE Bool IsFinal() const { return 0 != ( m_flags & FF_FinalFunction ); }

		//! Is this an event function ?
		RED_INLINE Bool IsEvent() const { return 0 != ( m_flags & FF_EventFunction ); }

		//! Is this an exec function ?
		RED_INLINE Bool IsExec() const { return 0 != ( m_flags & FF_ExecFunction ); }

		//! Is this function missing it's definition (meaning it's just a declaration, valid for native functions)
		RED_INLINE Bool HasUndefinedBody() const { return 0 != ( m_flags & FF_UndefinedBody ); }

		//! Is this a timer function ?
		RED_INLINE Bool IsTimer() const { return 0 != ( m_flags & FF_TimerFunction ); }

		//! Is this a private function ?
		RED_INLINE Bool IsPrivate() const { return 0 != ( m_flags & FF_PrivateFunction ); }

		//! Is this a protected function ?
		RED_INLINE Bool IsProtected() const { return 0 != ( m_flags & FF_ProtectedFunction ); }

		//! Is this a public function ?
		RED_INLINE Bool IsPublic() const { return 0 != ( m_flags & FF_PublicFunction ); }

		//! Is this a const function ?
		RED_INLINE Bool IsConst() const { return 0 != ( m_flags & FF_ConstFunction ); }

		//! Is this a quest function ?
		RED_INLINE Bool IsQuest() const { return 0 != ( m_flags & FF_QuestFunction ); }

		//! Is this function thread-safe ? In other words: can it be called at any time from any thread ?
		RED_INLINE Bool IsThreadSafe() const { return 0 != ( m_flags & FF_ThreadSafeFunction ); }

		//! Is this function replicable ?
		RED_INLINE Bool IsReplicable() const { return m_repFunction != nullptr; }

		//! Get corresponding replicated function; returns nullptr if function isn't replicable
		RED_INLINE const rep::Function*	GetRepFunction() const { return m_repFunction; }

		//! Get index of mapped native function
		virtual Uint32 GetNativeFunctionIndex() const { return std::numeric_limits<Uint32>::max(); }

		virtual const IFunctionCaller* GetNativeFunctionCaller() const { return nullptr; }

		//! Get function code ( for script functions )
		RED_INLINE const CScriptCompiledCode& GetCode() const { return m_code; }

		//! Get function code ( for script functions )
		RED_INLINE CScriptCompiledCode& GetCode() { return m_code; }

		//! Add function parameter
		void AddParameter( const CName name, const rtti::IType* type, Bool isOptional, Bool isSkipable, Bool isReference, const Property*& ret );

		//! Get number of function parameters
		RED_INLINE Uint32 GetNumParameters() const { return m_parameters.Size(); }

		//! Get function parameter
		RED_INLINE const Property* GetParameter( Uint32 index ) const { return m_parameters[ index ]; }

		//! Add function local variable
		void AddLocal( const CName name, const rtti::IType* type, const Property*& ret );

		//! Get number of local variables
		RED_INLINE Uint32 GetNumLocals() const { return m_localVars.Size(); }

		//! Get local variable
		RED_INLINE const Property* GetLocal( Uint32 index ) const { return m_localVars[ index ]; }

		//! Get function return value
		RED_INLINE const Property* GetReturnValue() const { return m_returnProperty; }

		//! Set function return type
		void SetReturnType( const rtti::IType* retType );

		//! Clear internal properties and other stuff
		void Reset();

		//! Find property by name
		const Property* FindProperty( CName propName ) const;

		//! Get all function properties
		void GetProperties( red::DynArray< const Property* >& properties ) const;

		//! Call function, used by scripting system
		Bool Call_ScriptsSystem( IScriptable* context, CScriptStackFrame& stackFrame, void* result, const rtti::IType* resultType ) const;

		//! Call function
		Bool Call( ScriptedFunctionContext& ctx, CScriptStackFrame* stackFrame = nullptr ) const;

		//! Call function
		Bool Call( ScriptedOrNativeFunctionContext& ctx ) const;

		//! Calculate function data layout of properties
		void CalcDataLayout();

	protected:

		struct OutputParam
		{
			const rtti::IType* m_type;
			void* m_dataOffset;
			void* m_localOffset;
		};

		typedef red::StaticArray< OutputParam, 8 > OutputParams;
		typedef red::DynArray< const Property* > PropertyList;

		CName							m_name;				//!< Name of the function
		CName							m_familyName;		//!< Name of the family which function belongs to
		Property*						m_returnProperty;	//!< If not nullptr function returns value via this property
		mutable const rep::Function*	m_repFunction;		//!< Corresponding replicated function
		PropertyList					m_parameters;		//!< Function input parameters
		PropertyList					m_localVars;		//!< Local variables
	
		CScriptCompiledCode				m_code;				//!< Compiled code ( for scripted function )
		Uint32							m_flags;			//!< Function flags
		Uint32							m_size;				//!< Size of data ( for scripted function )
		
		//! Handles replicated call originating from scripts
		Bool ReplicatedCall( IScriptable* context, CScriptStackFrame& stackFrame ) const;

		//! Call native function
		Bool CallNative( FunctionContext& ctx, CScriptStackFrame& stackFrame ) const;

		//! Call scripted function
		Bool CallScripted( CScriptStackFrame& frame, void* result, const rtti::IType* resultType ) const;

		//! Evaluate params on stack by executing stack frame code
		void InitializeParams( CScriptStackFrame& parentStackFrame, void *stack, OutputParams* outputParams ) const;

		//! Clean up function parameters
		void DestroyParams( CScriptStackFrame& frame, void* stack, void* result, const rtti::IType* resultType ) const;

		friend class rep::RTTIService;
	};

	class RED_REFLECTION_API NativeMemberFunction : public Function
	{
	public:
		// Create wrapper for native member function
		NativeMemberFunction( const rtti::ClassType* parentClass, CName name, CName familyName, TNativeFunc func, Uint32 flags = 0 );

		// Create wrapper for native static function
		NativeMemberFunction( const rtti::ClassType* parentClass, CName name, CName familyName, TNativeGlobalFunc func, Uint32 flags = 0 );

		NativeMemberFunction( NativeMemberFunction&& other );

		//! Get class this function is defined in
		virtual const rtti::ClassType* GetClass() const override final { return m_class; }

		//! Get index of mapped native function
		virtual Uint32 GetNativeFunctionIndex() const override final { return m_nativeFunction; }

	private:
		const rtti::ClassType* m_class; //!< Class this function is defined in
		Uint32 m_nativeFunction; //!< Index to mapped native function
	};

	class RED_REFLECTION_API NativeGlobalFunction : public Function
	{
	public:
		// Create wrapper for native global function
		NativeGlobalFunction( CName name, CName familyName, TNativeGlobalFunc func );

		NativeGlobalFunction( NativeGlobalFunction&& other );

		//! Get index of mapped native function
		virtual Uint32 GetNativeFunctionIndex() const override final { return m_nativeFunction; }

	private:
		Uint32 m_nativeFunction; //!< Index to mapped native function
	};

	class RED_REFLECTION_API ScriptedMemberFunction : public Function
	{
	public:
		// Create wrapper for native member function
		ScriptedMemberFunction( const rtti::ClassType* parentClass, CName name, CName familyName, Uint32 flags = 0 );

		ScriptedMemberFunction( ScriptedMemberFunction&& other );

		//! Get class this function is defined in
		virtual const rtti::ClassType* GetClass() const override final { return m_class; }

	private:
		const rtti::ClassType* m_class; //!< Class this function is defined in
	};

	template< class Type >
	class NativeFunctionCaller : public Function
	{
	public:
		// Create function containing custom native function caller (for non-IScriptable context)
		NativeFunctionCaller( const rtti::ClassType* parentClass, CName name, CName familyName, TFunctionCaller< Type >&& funcCaller, Uint32 flags = 0 )
			: Function( name, familyName, flags | FF_NativeFunction )
			, m_class( parentClass )
			, m_nativeFunctionCaller( std::move( funcCaller ) )
		{}

		NativeFunctionCaller( NativeFunctionCaller&& other )
			: Function( std::move( other ) )
			, m_class( other.m_class )
			, m_nativeFunctionCaller( std::move( other.m_nativeFunctionCaller ) )
		{}

		//! Get class this function is defined in
		virtual const rtti::ClassType* GetClass() const override final { return m_class; }

		virtual const IFunctionCaller* GetNativeFunctionCaller() const override final { return &m_nativeFunctionCaller; }

	private:
		const rtti::ClassType* m_class; //!< Class this function is defined in
		TFunctionCaller< Type > m_nativeFunctionCaller; // only used with non-IScriptable based classes
	};

} // rtti