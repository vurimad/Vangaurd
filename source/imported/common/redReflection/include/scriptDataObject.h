/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

//---------------------------------------------------------------------------
// This file contains data structures to hold script data both in memory and on disk
//---------------------------------------------------------------------------

class CScriptedDataTypeRef;
class CScriptedDataClass;
class CScriptedDataEnum;
class CScriptedDataBitfield;
class CScriptedDataFunction;
class CScriptedDataFunctionLocal;
class CScriptedDataFunctionParam;
class CScriptedDataProperty;

class IScriptDataSaver;
class IScriptDataLoader;

namespace rtti
{
	class IType;
	class ClassType;
	class EnumType;
	class BitFieldType;
	class Property;
	class Function;
}

//---------------------------------------------------------------------------

struct RED_REFLECTION_API ScriptDataSourceContext
{
	RED_USE_MEMORY_POOL( red::PoolScript );

	ScriptDataSourceContext() = default;
	ScriptDataSourceContext( const ScriptDataSourceContext& ) = default;
	ScriptDataSourceContext( ScriptDataSourceContext&& ) = default;
	ScriptDataSourceContext& operator=( const ScriptDataSourceContext& ) = default;
	ScriptDataSourceContext& operator=( ScriptDataSourceContext&& ) = default;

	String m_file;
	Uint32 m_startPosition;
	Uint32 m_endPosition;
};

/// Scripting system data object (pre-RTTI mapped)
class RED_REFLECTION_API IScriptDataObject
{
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolScript );

public:
	IScriptDataObject( const CName name );
	virtual ~IScriptDataObject() = default;

	// meta type
	enum class EMetaType : Uint8
	{
		TypeRef,
		Class,
		NamedValue,
		Enum,
		Bitfield,
		Function,
		FunctionParam,
		FunctionLocal,
		Property,
		FileInfo
	};
	
	// general visibility (stored)
	enum class EVisibility : Uint8
	{
		Public,
		Protected,
		Private,
	};

	// get type
	virtual EMetaType GetMetaType() const = 0;

	// get visibility access
	virtual EVisibility GetVisibility() const = 0;

	// get parent object (object that owns this)
	virtual const IScriptDataObject* GetParent() const = 0;

	// get base data object (object that is extended by this, e.g. base class, array/handle internal type)
	virtual const IScriptDataObject* GetBase() const = 0;

	// get name (everything has name here)
	RED_INLINE const CName GetName() const { return m_name; }

	// is this imported (native) object ?
	virtual const Bool IsImported() const = 0;

	// save to stream
	virtual void Save( class IScriptDataSaver& saver ) const = 0;

	// load from stream
	virtual void Load( class IScriptDataLoader& loader ) = 0;

	const ScriptDataSourceContext* GetContext() const;
	void SetContext( red::UniquePtr< ScriptDataSourceContext >& context );

protected:
	CName m_name;

#ifndef RED_CONFIGURATION_FINAL
	// Only used by SCC
	red::UniquePtr< ScriptDataSourceContext > m_context;
#endif
};

//---------------------------------------------------------------------------

/// Scripted file information
class RED_REFLECTION_API CScriptedDataFileInfo : public IScriptDataObject
{
public:
	CScriptedDataFileInfo();
	void operator= ( const CScriptedDataFileInfo& other );
	~CScriptedDataFileInfo();

	RED_INLINE Uint32 GetHash() const { return m_hashedPath; }
	void SetHash( Uint32 hash );

	RED_INLINE Uint32 GetCRC() const { return m_fileCRC; }
	void SetCRC( Uint32 crc );

	const String& GetRelativePath() const;
	void SetRelativePath( const String& path );

	RED_INLINE const Int32 GetFileIndex() const { return m_fileIndex; }
	void SetFileIndex( Int32 index );

private:
	Uint32		m_fileCRC; //<! crc of source code
	Int32		m_fileIndex; //!< file index relative to compilation order
	Uint32		m_hashedPath; //!< hash of the relative path

#if !defined( RED_PLATFORM_CONSOLE) || !defined( RED_CONFIGURATION_FINAL )
	String	m_relativePath; //!< source file relative path
#endif

	virtual EMetaType GetMetaType() const override { return EMetaType::FileInfo; }
	virtual EVisibility GetVisibility() const override { return EVisibility::Public; }
	virtual const IScriptDataObject* GetParent() const override { return nullptr; }
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return false; }

	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;
};

//---------------------------------------------------------------------------

/// Reference to type, allows type resolution
class RED_REFLECTION_API CScriptedDataTypeRef : public IScriptDataObject
{
public:
	enum EType
	{
		Named, // named
		Internal, // internal class/enum/bitfield etc reference
		StrongHandle, // strong handle overlay
		WeakHandle, // weak handle overlay
		DynArray, // dynamic array overlay
		StaticArray, // static array overlay
		Reference, // non-atomic reference
	};

	CScriptedDataTypeRef(); // void
	CScriptedDataTypeRef( const CName name ); // deserialization
	CScriptedDataTypeRef( const EType type, const CName name );
	CScriptedDataTypeRef( const EType type, const CName name, const IScriptDataObject* internalRef, Int32 countForArray=-1 );
	~CScriptedDataTypeRef();

	RED_INLINE const EType GetType() const { return m_type;	}
	RED_INLINE const IScriptDataObject* GetInternal() const { return m_internal; }
	RED_INLINE const Int32 GetArrayCount() const { return m_arrayCount; }
	RED_INLINE const Bool IsHandleType() const { return m_type == StrongHandle || m_type == WeakHandle; }
	RED_INLINE const Bool IsWeakHandleType() const { return m_type == WeakHandle; }
	RED_INLINE const Bool IsArrayType() const { return m_type == DynArray || m_type == StaticArray; }
	RED_INLINE const Bool IsReferenceType() const { return m_type == Reference; }

	const String ToString() const;

	// resolved at runtime
	mutable const rtti::IType*	m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::TypeRef; }
	virtual const IScriptDataObject* GetParent() const override { return nullptr; } // for now we don't support nested classes
	virtual const IScriptDataObject* GetBase() const override;
	virtual const Bool IsImported() const override { return false; } // typerefs are never imported
	virtual EVisibility GetVisibility() const override { return EVisibility::Public; } // we don't protect this stuff
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

private:
	const IScriptDataObject*	m_internal;		// internal object
	Int32						m_arrayCount;	// static array size, ehhh
	EType						m_type;			// kind of the type reference
};

//---------------------------------------------------------------------------

/// Scripted class meta information
class RED_REFLECTION_API CScriptedDataClass : public IScriptDataObject
{
public:
	CScriptedDataClass( const CName name );
	virtual ~CScriptedDataClass();

	CScriptedDataClass& operator=( const CScriptedDataClass& other );

	void SetVisibility( EVisibility visibility );

	typedef red::DynArray< CScriptedDataProperty* >		TProps;
	typedef red::DynArray< CScriptedDataFunction* >		TFunctions;

	RED_INLINE CScriptedDataClass* GetBaseClass() const { return m_baseClass; }
	void SetBaseClass( CScriptedDataClass* baseClass );

	CScriptedDataFunction* AddFunction( CName functionName, CName familyName, EVisibility visibility );
	CScriptedDataProperty* AddProperty( CName propertyName, EVisibility visibility );
	CScriptedDataProperty* AddOverridenProperty( CName propertyName, EVisibility visibility );

	RED_INLINE const TProps& GetProperties() const { return m_properties; }
	RED_INLINE const TProps& GetOverridenProperties() const { return m_overridenProperties; }
	RED_INLINE const TFunctions& GetFunctions() const { return m_functions; } 

	RED_INLINE const Bool IsAbstract() const { return m_isAbstract; }
	RED_INLINE const Bool IsFinal() const { return m_isFinal; }
	RED_INLINE const Bool IsImportOnly() const { return m_isImportOnly; }
	RED_INLINE const Bool IsStructure() const { return m_isStructure; }
	RED_INLINE const Bool IsTestOnly() const { return m_isTestOnly; }
	RED_INLINE const Bool IsPrivate() const { return m_visibility == EVisibility::Private; }
	RED_INLINE const Bool IsProtected() const { return m_visibility == EVisibility::Protected; }

	void SetAbstract( const Bool isAbstrct );
	void SetFinal( const Bool isFinal );
	void SetImported( const Bool isImported );
	void SetImportOnly( const Bool isImportOnly );
	void SetStructure( const Bool isStructure );
	void SetTestOnly( const Bool isTestOnly );

	CScriptedDataFunction* FindFunction( CName functionName ) const; // recursive over base classes
	void EnumFunctionsFromFamily( CName familyName, red::Map< CName, const CScriptedDataFunction* >& functions ) const;
	CScriptedDataFunction* FindLocalFunction( CName functionName ) const; // this class only

	CScriptedDataProperty* FindProperty( CName propertyName ) const; // recursive over base classes
	CScriptedDataProperty* FindLocalProperty( CName propertyName ) const; // this class only
	CScriptedDataProperty* FindOverridenProperty( CName propertyName ) const; // this class only

	void GetAllFunctions( TFunctions& functions ) const;
	void GetAllProperties( TProps& props ) const;

	bool IsA( const CScriptedDataClass* baseClass ) const;

	// runtime resolve
	mutable const class rtti::ClassType*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::Class; }
	virtual const IScriptDataObject* GetParent() const override { return nullptr; } // for now we don't support nested classes
	virtual const IScriptDataObject* GetBase() const override { return GetBaseClass(); }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return m_visibility; } // we don't protect this stuff
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:

	enum : Uint16
	{
		SDCF_IsImported			= RED_FLAG( 0 ),
		SDCF_IsAbstract			= RED_FLAG( 1 ),
		SDCF_IsFinal			= RED_FLAG( 2 ),
		SDCF_IsStructure		= RED_FLAG( 3 ),
		SDCF_HasFunctions		= RED_FLAG( 4 ),
		SDCF_HasProps			= RED_FLAG( 5 ),
		SDCF_IsImportOnly		= RED_FLAG( 6 ),
		SDCF_IsTestOnly			= RED_FLAG( 7 ),
		SDCF_HasOverridenProps	= RED_FLAG( 8 )
	};

	CScriptedDataClass*		m_baseClass;
	TProps					m_properties{ red::PoolScript() };
	TProps					m_overridenProperties{ red::PoolScript() };
	TFunctions				m_functions{ red::PoolScript() };

	EVisibility				m_visibility;

	Bool					m_isImported:1;
	Bool					m_isImportOnly:1;
	Bool					m_isAbstract:1;
	Bool					m_isFinal:1;
	Bool					m_isStructure:1;
	Bool					m_isTestOnly:1;
};

//---------------------------------------------------------------------------

/// Scripted named value
class RED_REFLECTION_API CScriptedDataNamedValue : public IScriptDataObject
{
public:
	CScriptedDataNamedValue( const CName name, IScriptDataObject* parent );

	RED_INLINE const Int64 GetValue() const { return m_value; }
	void SetValue( const Int64 value );
	void SetImported( const Bool isImported );

	mutable Int64 m_resolvedValue; 

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::NamedValue; }
	virtual const IScriptDataObject* GetParent() const override { return m_parent; }
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return EVisibility::Public; } // we don't protect this stuff
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:
	IScriptDataObject*		m_parent;
	Int64					m_value;
	Bool					m_isImported;
};

//---------------------------------------------------------------------------

/// Scripted enum meta information
class RED_REFLECTION_API CScriptedDataEnum : public IScriptDataObject
{
public:
	CScriptedDataEnum( const CName name );
	virtual ~CScriptedDataEnum();

	CScriptedDataEnum& operator=( const CScriptedDataEnum& other );

	typedef red::DynArray< CScriptedDataNamedValue* > TValues;

	CScriptedDataNamedValue* AddValue( const CName name, Int64 value );
	void SetImported( const Bool isImported );
	void SetSize( const Uint8 size );

	RED_INLINE const TValues& GetValues() const { return m_values; }
	RED_INLINE const Uint32 GetSize() const { return m_size; }

	CScriptedDataNamedValue* FindValue( const CName name ) const;

	// runtime resolve
	mutable const class rtti::EnumType*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::Enum; }
	virtual const IScriptDataObject* GetParent() const override { return nullptr; } // for now we don't support nested enums
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return m_visibility; }
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:
	TValues			m_values{ red::PoolScript() };
	Uint8			m_size;
	Bool			m_isImported;
	EVisibility		m_visibility;
};

//---------------------------------------------------------------------------

/// Scripted bitfield meta information
class RED_REFLECTION_API CScriptedDataBitfield : public IScriptDataObject
{
public:
	CScriptedDataBitfield( const CName name );
	virtual ~CScriptedDataBitfield();

	CScriptedDataBitfield& operator=( const CScriptedDataBitfield& other );

	typedef red::DynArray< CScriptedDataNamedValue* > TValues;

	void AddBit( const CName name, Uint64 value );
	void SetImported( const Bool isImported );
	void SetSize( const Uint8 size );

	RED_INLINE const TValues& GetValues() const { return m_values; }
	RED_INLINE const Uint32 GetSize() const { return m_size; }

	// runtime resolve
	mutable const class rtti::BitFieldType*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::Bitfield; }
	virtual const IScriptDataObject* GetParent() const override { return nullptr; } // for now we don't support nested bitfields
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return m_visibility; }
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:
	TValues			m_values{ red::PoolScript() };
	Uint8			m_size;
	Bool			m_isImported;
	EVisibility		m_visibility;
};

//---------------------------------------------------------------------------

/// Scripted property meta information
class RED_REFLECTION_API CScriptedDataProperty : public IScriptDataObject
{
public:
	CScriptedDataProperty( const CName name, IScriptDataObject* parent, EVisibility visibility );

	CScriptedDataProperty& operator=( const CScriptedDataProperty& other );

	RED_INLINE const Bool IsEditable() const { return m_isEditable; }
	RED_INLINE const Bool IsInstanceEditable() const { return m_isInstanceEditable; }
	RED_INLINE const Bool IsConst() const { return m_isConst; }	
	RED_INLINE const Bool IsInlined() const { return m_isInlined; }
	RED_INLINE const Bool IsReplicated() const { return m_isReplicated; }
	RED_INLINE const Bool IsPersistent() const { return m_isPersistent; }
	RED_INLINE const Bool IsTestOnly() const { return m_isTestOnly; }
	RED_INLINE const Bool IsBrowsable() const { return m_isBrowsable; }
	const String& GetHint() const;
	RED_INLINE const String& GetDefaultValue( const String& typeName ) const { return m_defaultValues[ typeName ]; }
	RED_INLINE const red::Map< String, String >& GetDefaultValues() const { return m_defaultValues; }
	RED_INLINE const Bool HasDefaultValue() const { return !m_defaultValues.Empty(); }
	RED_INLINE const CScriptedDataTypeRef* GetType() const { return m_type; }

	void SetType( const CScriptedDataTypeRef* typeRef );
	void SetInlined( const Bool isInlined );
	void SetEditable( const Bool isEditable );
	void SetInstanceEditable( const Bool isEditable );
	void SetConst( const Bool isConst );
	void SetReplicated( const Bool isReplicated );
	void SetPersistent( const Bool isPersistent );
	void SetImported( const Bool isImported );
	void SetTestOnly( const Bool isTestOnly );
	void SetBrowsable( const Bool isBrowsable );
	void SetHint( const String& hint );
	void SetDefaultValue( const String& typeName, const String& value );

	void SetAttribute( const red::StringView& name, const red::StringView& value );
	const red::String& GetAttributeValue( const char* name ) const;

	// runtime resolve
	mutable const class rtti::Property*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::Property; }
	virtual const IScriptDataObject* GetParent() const override { return m_parent; }
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return m_visibility; }
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:

	enum : Uint16
	{
		SDPF_IsImported		= RED_FLAG( 0 ),
		SDPF_IsEditable		= RED_FLAG( 1 ),
		SDPF_IsInlined		= RED_FLAG( 2 ),
		SDPF_IsConst		= RED_FLAG( 3 ),
		SDPF_IsReplicated	= RED_FLAG( 4 ),
		SDPF_HasHint		= RED_FLAG( 5 ),
		SDPF_IsInstanceEditable = RED_FLAG( 6 ),
		SDPF_HasDefaultValue = RED_FLAG( 7 ),
		SPDF_IsPersistent = RED_FLAG( 8 ),
		SPDF_IsTestOnly = RED_FLAG( 9 ),
		SPDF_IsBrowsable = RED_FLAG( 10 )
	};

	IScriptDataObject*	m_parent;

	Bool m_isImported:1;
	Bool m_isEditable:1;
	Bool m_isInlined:1;
	Bool m_isConst:1;
	Bool m_isReplicated:1;
	Bool m_isInstanceEditable:1;
	Bool m_isPersistent:1;
	Bool m_isTestOnly:1;
	Bool m_isBrowsable:1;

#ifndef RED_CONFIGURATION_FINAL
	String m_hint;
#endif
	red::Map< String, String > m_defaultValues{ red::PoolScript() };
	EVisibility	m_visibility;

	red::HashMap< CName, red::String > m_attributes{ red::PoolScript() };

	const CScriptedDataTypeRef* m_type;
};

//---------------------------------------------------------------------------

/// Scripted function meta information
class RED_REFLECTION_API CScriptedDataFunction : public IScriptDataObject
{
public:

	class RED_REFLECTION_API ReturnValue
	{
	public:
		ReturnValue();
		ReturnValue& operator=( const ReturnValue& other );
		RED_INLINE const CScriptedDataTypeRef* GetType() const { return m_type; }
		RED_INLINE const Bool IsConst() const { return m_isConst; }
		RED_INLINE Bool operator==( const ReturnValue& other ) const { return m_type == other.m_type && m_isConst == other.m_isConst; }
		RED_INLINE Bool operator!=( const ReturnValue& other ) const { return !operator==( other ); }
		RED_INLINE void SetType( const CScriptedDataTypeRef* type ) { m_type = type; }
		RED_INLINE void SetConst( const Bool isConst ) { m_isConst = isConst; }
		void Save( class IScriptDataSaver& saver ) const;
		void Load( class IScriptDataLoader& loader );

	private:

		enum : Uint8
		{
			SDFRVF_IsConst		= RED_FLAG( 0 ),
		};

		const CScriptedDataTypeRef*	m_type;
		Bool m_isConst : 1;
	};

	CScriptedDataFunction( const CName name, const CName familyName, IScriptDataObject* parent, EVisibility visibility );
	~CScriptedDataFunction();

	CScriptedDataFunction& operator=( const CScriptedDataFunction& other );

	typedef red::DynArray< CScriptedDataFunctionParam* >		TParams;
	typedef red::DynArray< CScriptedDataFunctionLocal* >		TLocals;

	RED_INLINE const CName GetFamilyName() const { return m_familyName; }
	RED_INLINE const TParams& GetParams() const { return m_params; }
	RED_INLINE const TLocals& GetLocals() const { return m_locals; }
	RED_INLINE ReturnValue& GetReturnValue() { return m_returnValue; }
	RED_INLINE const ReturnValue& GetReturnValue() const { return m_returnValue; }

	RED_INLINE const void* GetCode() const { return m_rawCode.Data(); }
	RED_INLINE const Uint32 GetCodeSize() const { return (Uint32)m_rawCode.DataSize(); }

	RED_INLINE const CScriptedDataFunction* GetSuperFunc() const { return m_superFunc; }

	RED_INLINE const Bool IsGlobal() const { return m_parent == nullptr; }

	RED_INLINE const Bool IsStatic() const { return m_isStatic; }
	RED_INLINE const Bool IsExec() const { return m_isExec; }
	RED_INLINE const Bool IsTimer() const { return m_isTimer; }
	RED_INLINE const Bool IsFinal() const { return m_isFinal; }
	RED_INLINE const Bool IsImportedOverride() const { return m_isImportedOverride; }
	RED_INLINE const Bool IsEvent() const { return m_isEvent; }
	RED_INLINE const Bool IsOperator() const { return m_isOperator; }
	RED_INLINE const Bool IsCast() const { return m_isCast; }
	RED_INLINE const Bool IsCastImplicit() const { return m_isCastImplicit; }
	RED_INLINE const Bool IsMulticast() const { return m_isMulticast; }
	RED_INLINE const Bool IsHost() const { return m_isHost; }
	RED_INLINE const Bool IsClient() const { return m_isClient; }
	RED_INLINE const Bool IsReliable() const { return m_isReliable; }
	RED_INLINE const Bool IsReplicable() const { return m_isMulticast || m_isHost || m_isClient; }
	RED_INLINE const Bool IsConst() const { return m_isConst; }
	RED_INLINE const Bool IsThreadSafe() const { return m_isThreadSafe; }
	RED_INLINE const Bool IsQuest() const { return m_isQuest; }
	RED_INLINE const Bool IsTestOnly() const { return m_isTestOnly; }
	RED_INLINE const Uint8 GetCastCost() const { return m_castCost; }
	RED_INLINE const Uint16 GetSourceLine() const { return m_sourceLine; }
	RED_INLINE const CScriptedDataFileInfo* const GetFileInfo() const { return m_sourceFileInfo; }
	RED_INLINE const CName GetOperationName() const { return m_operationName; }
	
	CScriptedDataFunctionParam* AddParam( const CName name );
	CScriptedDataFunctionLocal* AddLocal( const CName name );

	CScriptedDataFunctionParam* FindParam( const CName name ) const;
	CScriptedDataFunctionLocal* FindLocal( const CName name ) const;

	void SetStatic( const Bool isStatic );
	void SetExec( const Bool isExec );
	void SetTimer( const Bool isTimer );
	void SetFinal( const Bool isFinal );
	void SetImported( const Bool isImported );
	void SetImportedOverride( const Bool isImportedOverride );
	void SetEvent( const Bool isEvent );
	void SetOperator( const Bool isOperator );
	void SetOperationName( const CName name );
	void SetCast( const Bool isCast );
	void SetCastImplicit( const Bool isCastImplicit );
	void SetCastCost( const Uint8 castCost );
	void SetMulticast( const Bool isMulticast );
	void SetServer( const Bool isServer );
	void SetClient( const Bool isClient );
	void SetReliable( const Bool isReliable );
	void SetConst( const Bool isConst );
	void SetThreadSafe( const Bool isThreadSafe );
	void SetQuest( const Bool isQuest );
	void SetTestOnly( const Bool isTestOnly );
	void SetSuperFunction( const CScriptedDataFunction* superFunc );
	void SetSourceLine ( Int32 lineNumber ) { m_sourceLine = lineNumber; }
	void SetRawCode( const void* code, const Uint32 codeSize );
	void SetFileInfo( const CScriptedDataFileInfo* fileInfo );

	static CName CreateFamilyName( CName functionName );
	red::String CreateDebugName() const;
	void UndecorateName();

	void SetAttribute( const red::StringView& name, const red::StringView& value );
	const red::String& GetAttributeValue( const char* name ) const;

	// runtime resolve
	mutable const class rtti::Function*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::Function; }
	virtual const IScriptDataObject* GetParent() const override { return m_parent; } // can be null
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return m_isImported; }
	virtual EVisibility GetVisibility() const override { return m_visibility; } // we don't protect this stuff
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:

	enum : Uint32
	{
		SDFF_IsStatic		= RED_FLAG( 0 ),
		SDFF_IsExec			= RED_FLAG( 1 ),
		SDFF_IsTimer		= RED_FLAG( 2 ),
		SDFF_IsFinal		= RED_FLAG( 3 ),
		SDFF_IsImported		= RED_FLAG( 4 ),
		SDFF_IsEvent		= RED_FLAG( 5 ),
		SDFF_IsOperator		= RED_FLAG( 6 ),
		SDFF_HasRetType		= RED_FLAG( 7 ),
		SDFF_HasSuperFunc	= RED_FLAG( 8 ),
		SDFF_HasParams		= RED_FLAG( 9 ),
		SDFF_HasLocals		= RED_FLAG( 10 ),
		SDFF_HasRawCode		= RED_FLAG( 11 ),
		SDFF_IsCast			= RED_FLAG( 12 ),
		SDFF_IsCastImplicit	= RED_FLAG( 13 ),
		SDFF_IsMulticast	= RED_FLAG( 14 ),
		SDFF_IsServer		= RED_FLAG( 15 ),
		SDFF_IsClient		= RED_FLAG( 16 ),
		SDFF_IsReliable		= RED_FLAG( 17 ),
		SDFF_IsConst		= RED_FLAG( 18 ),
		SDFF_IsThreadSafe	= RED_FLAG( 19 ),
		SDFF_IsQuest		= RED_FLAG( 20 ),
		SDFF_IsTestOnly		= RED_FLAG( 21 )
	};

	IScriptDataObject*		m_parent;

	CName					m_familyName;		// name without arguments types
	TParams					m_params;
	TLocals					m_locals;
	ReturnValue				m_returnValue;

	const CScriptedDataFunction* m_superFunc;

	red::HashMap< CName, red::String > m_attributes{ red::PoolScript() };

	Bool					m_isStatic:1;
	Bool					m_isExec:1;
	Bool					m_isTimer:1;
	Bool					m_isFinal:1;
	Bool					m_isImported:1;
	Bool					m_isImportedOverride:1;
	Bool					m_isEvent:1;
	Bool					m_isOperator:1;
	Bool					m_isCast:1;
	Bool					m_isCastImplicit:1;
	Bool					m_isMulticast:1;
	Bool					m_isHost:1;
	Bool					m_isClient:1;
	Bool					m_isReliable:1;
	Bool					m_isConst:1;
	Bool					m_isThreadSafe:1;
	Bool					m_isQuest:1;
	Bool					m_isTestOnly:1;

	CName					m_operationName;	// only for operators
	Uint8					m_castCost;			// only for cast functions
	EVisibility				m_visibility;

	// Code info
	typedef red::DynArray< Uint8 >	TRawCode;
	TRawCode						m_rawCode;

	const CScriptedDataFileInfo*	m_sourceFileInfo; //<! pointer to the source file info of the file a function is in
	Uint32							m_sourceLine;
};

//---------------------------------------------------------------------------

/// Scripted function parameter meta information
class RED_REFLECTION_API CScriptedDataFunctionParam : public IScriptDataObject
{
public:
	CScriptedDataFunctionParam( const CName name, CScriptedDataFunction* func );

	CScriptedDataFunctionParam& operator=( const CScriptedDataFunctionParam& other );

	RED_INLINE CScriptedDataFunction* GetFunction() const { return m_function; }

	RED_INLINE const Bool IsOptional() const { return m_isOptional; }
	RED_INLINE const Bool IsReference() const { return m_isRef; }
	RED_INLINE const Bool IsSkipped() const { return m_isSkipped; }
	RED_INLINE const Bool IsConst() const { return m_isConst; }

	RED_INLINE const CScriptedDataTypeRef* GetType() const { return m_type; }

	void SetType( const CScriptedDataTypeRef* type );
	void SetOptional( const Bool isOptional );
	void SetReference( const Bool isReference );
	void SetSkipped( const Bool isSkipped );
	void SetConst( const Bool isConst );

	// runtime resolve
	mutable const class rtti::Property*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::FunctionParam; }
	virtual const IScriptDataObject* GetParent() const override { return m_function; }
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual const Bool IsImported() const override { return false; }
	virtual EVisibility GetVisibility() const override { return EVisibility::Public; } // we don't protect this stuff
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:

	enum : Uint8
	{
		SDFPF_IsOptional	= RED_FLAG( 0 ),
		SDFPF_IsRef			= RED_FLAG( 1 ),
		SDFPF_IsSkipped		= RED_FLAG( 2 ),
		SDFPF_IsConst		= RED_FLAG( 3 ),
	};

	CScriptedDataFunction*		m_function;
	const CScriptedDataTypeRef* m_type;

	Bool						m_isOptional:1;
	Bool						m_isRef:1;
	Bool						m_isSkipped:1;
	Bool						m_isConst:1;
};

//---------------------------------------------------------------------------

/// Scripted function local variable meta information
class RED_REFLECTION_API CScriptedDataFunctionLocal : public IScriptDataObject
{
public:
	CScriptedDataFunctionLocal( const CName name, CScriptedDataFunction* func );

	CScriptedDataFunctionLocal& operator=( const CScriptedDataFunctionLocal& other );

	RED_INLINE CScriptedDataFunction* GetFunction() const { return m_function; }
	RED_INLINE const CScriptedDataTypeRef* GetType() const { return m_type; }
	RED_INLINE const Bool IsConst() const { return m_isConst; }

	void SetType( const CScriptedDataTypeRef* type );
	void SetConst( const Bool isConst );

	// runtime resolve
	mutable const class rtti::Property*		m_resolvedPtr;

public:
	// IScriptDataObject interface
	virtual EMetaType GetMetaType() const override { return EMetaType::FunctionLocal; }
	virtual const IScriptDataObject* GetParent() const override { return m_function; }
	virtual const IScriptDataObject* GetBase() const override { return nullptr; }
	virtual EVisibility GetVisibility() const override { return EVisibility::Public; } // we don't protect this stuff
	virtual const Bool IsImported() const override { return false; }
	virtual void Save( class IScriptDataSaver& saver ) const override;
	virtual void Load( class IScriptDataLoader& loader ) override;

protected:

	enum : Uint8
	{
		SDFLF_IsConst = RED_FLAG( 0 ),
	};

	CScriptedDataFunction*			m_function;
	const CScriptedDataTypeRef*		m_type;

	Bool m_isConst : 1;
};

//---------------------------------------------------------------------------
