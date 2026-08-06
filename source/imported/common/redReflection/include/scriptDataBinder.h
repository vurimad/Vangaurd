/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "scriptOpcodeTransformer.h"
#include "scriptFile.h"
#include "scriptInstrumentationObjectRuntime.h"
#include "resourceReferenceScriptToken.h"

// Target RTTI system
namespace rtti
{
	class ITypeSystem;
} // rtti

namespace rep
{
	class Type;
}

typedef std::function< CScriptFile* (const Uint32 fileIndex) > TScriptFileBinder;

/// Helper class that binds script data loaded from file with current runtime
/// NOTE: we are aiming to do minimal updates - so if the topology of the RTTI is not changing we are NOT updating it
/// This way we can reload script functions faster without the full rebuilt
class RED_REFLECTION_API CScriptDataBinder
{
public:
	CScriptDataBinder(rtti::ITypeSystem& rtti, const TScriptFileBinder& fileBinder);

	/// Apply script data into the RTTI system
	Bool BindScripts( const class CScriptedDataEnvironment& env, class IScriptDataErrorReporter& err ) const;

private:
	// Create file info objects used by scripting system
	void CreateFiles( const red::DynArray< const CScriptedDataFileInfo *>& allFiles ) const;

	// Resolve imported objects, make sure everything exists
	Bool ResolveImports( const red::DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const;

	// Resolve type references
	void ResolveTypeRefs( const red::DynArray< const IScriptDataObject* >& allObjects ) const;

	// create scripted enums, bitfields and classes
	Bool CreateTypes( const red::DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const;

	// create properties in classes
	Bool CreateClassProperties( const red::DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const;

	// create functions
	Bool CreateFunctions( const red::DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const;

	// load opcodes for all functions
	void LoadOpcodes( const red::DynArray< const IScriptDataObject* >& allObjects ) const;

private:
	// create scripted enum
	Bool CreateEnum( const class CScriptedDataEnum& obj, class IScriptDataErrorReporter& err ) const;

	// create scripted bitfield
	Bool CreateBitfield( const class CScriptedDataBitfield& obj, class IScriptDataErrorReporter& err ) const;

	// create scripted class
	Bool CreateClass( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const;

	// create scripted function
	Bool CreateFunction( const class CScriptedDataFunction& obj, class IScriptDataErrorReporter& err ) const;

	// create scripted class
	Bool CreateClassProperties( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const;

	// replication
	void AddReplicatedProperty( const rtti::ClassType* cls, const rtti::Property* property, const CScriptedDataProperty& scriptProperty ) const;
	void SetupFunctionReplication( const CScriptedDataFunction& func ) const;

	// load opcodes for function
	void LoadOpcodes( const class CScriptedDataFunction& obj ) const;

private:
	rtti::ITypeSystem*		m_rtti;
	TScriptFileBinder		m_fileBinder;
};

namespace Helper
{
	class CodeMemoryToRuntime : public CScriptOpcodeTransformer
	{
	public:
		CodeMemoryToRuntime( void* data, const Uint32 size )
			: m_readPos( 0 )
			, m_writePos( 0 )
			, m_size( size )
			, m_data( (Uint8*) data )
			, m_breakpoints( red::PoolScript() )
			, m_profileCodeOffset( -1 )
		{}

		virtual Bool EndOfStream() const override
		{
			return m_readPos >= m_size;
		}

		virtual Uint8 ProcessOpcode() override
		{
			const auto op = Read<Uint8>();
			Write( op );
			return op;
		}

		red::DynArray< script::RuntimeBreakpoint >&& GetBreakpoints() { return std::move( m_breakpoints ); }

		Int32 GetProfileCodeOffset() const { return m_profileCodeOffset; };

		virtual void ProcessBreakpoint() override;

		virtual void ProcessStartProfile() override;

		virtual void ProcessInt8() override
		{
			Write( Read<Int8>() );
		}

		virtual void ProcessInt16() override
		{
			Write( Read<Int16>() );
		}

		virtual void ProcessInt32() override
		{
			Write( Read<Int32>() );
		}

		virtual void ProcessInt64() override
		{
			Write( Read<Int64>() );
		}

		virtual void ProcessUint8() override
		{
			Write( Read<Uint8>() );
		}

		virtual void ProcessUint16() override
		{
			Write( Read<Uint16>() );
		}

		virtual void ProcessUint32() override
		{
			Write( Read<Uint32>() );
		}

		virtual void ProcessUint64() override
		{
			Write( Read<Uint64>() );
		}

		virtual void ProcessLabel() override
		{
			Write( Read<Int16>() );
		}

		virtual void ProcessFloat() override
		{
			Write( Read<Float>() );
		}

		virtual void ProcessDouble() override
		{
			Write( Read<Double>() );
		}

		virtual void ProcessString() override
		{
			const Uint32 len = Read<Uint32>();
			Write( len );

			for ( Uint32 i = 0; i < len; ++i )
			{
				Write( Read<char>() );
			}
		}

		virtual void ProcessStaticString() override
		{
			ProcessString();
		}

		virtual void ProcessName() override
		{
			Write( Read< CName >() );
		}

		virtual void ProcessTweakDBID() override
		{
			Write( Read< TweakDBID >() );
		}

		virtual void ProcessResRef() override
		{
			Write( Read< red::ResourceReferenceScriptToken >() );
		}

		virtual void ProcessTypeRef() override;
		virtual void ProcessEnum() override;
		virtual void ProcessPointer() override;
		virtual void ProcessPropertyRef() override;
		virtual void ProcessLocalPropertyRef() override;
		virtual void ProcessParamPropertyRef() override;
		virtual void ProcessFunctionRef() override;
		virtual void ProcessNamedValueRef() override;

	private:
		// buffer
		Uint32			m_size;
		const Uint8*	m_data;

		// input
		Uint32			m_readPos;

		// output
		Uint32			m_writePos;

		// breakpoints
		red::DynArray< script::RuntimeBreakpoint > m_breakpoints;

		// profile data
		Int32 m_profileCodeOffset;

	protected:
		// read data
		template< typename T >
		RED_INLINE T Read()
		{
			RED_FATAL_ASSERT( m_readPos + sizeof(T) <= m_size, "Reading past stream end" );
			const T ret = *(const T*)( m_data + m_readPos );
			m_readPos += sizeof(T);
			return ret;
		}

		// write data
		template< typename T >
		RED_INLINE void Write( const T& data )
		{
			RED_FATAL_ASSERT( m_writePos + sizeof(T) <= m_size, "Reading past stream end" );
			*(T*)( m_data + m_writePos ) = data;
			m_writePos += sizeof(T);
		}
	};


	class CodeRuntimeToRuntime : public CodeMemoryToRuntime
	{
	public:
		CodeRuntimeToRuntime( class CScriptedDataEnvironment* const owner, void* data, const Uint32 size )
			: CodeMemoryToRuntime( data, size )
			, m_owner( owner )
		{}

		virtual void ProcessBreakpoint() override;
		virtual void ProcessStartProfile() override;
		virtual void ProcessTypeRef() override;
		virtual void ProcessEnum() override;
		virtual void ProcessPointer() override;
		virtual void ProcessPropertyRef() override;
		virtual void ProcessLocalPropertyRef() override;
		virtual void ProcessParamPropertyRef() override;
		virtual void ProcessFunctionRef() override;
		virtual void ProcessNamedValueRef() override;

	private:
		template< typename TScriptedData >
		void ProcessProperty();

		CScriptedDataEnvironment* m_owner;
	};
} // Helper