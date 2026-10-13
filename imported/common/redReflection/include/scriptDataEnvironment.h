/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//---------------------------------------------------------------------------

#include "../../../common/redContainers/include/redContainersPublic.h"
#include "../../../common/redSystem/include/readWriteSpinLock.h"

//---------------------------------------------------------------------------

class IScriptDataObject;
class CScriptedDataTypeRef;
class CScriptedDataClass;
class CScriptedDataEnum;
class CScriptedDataBitfield;
class CScriptedDataFunction;
class CScriptedDataProperty;
class CScriptedDataNamedValue;
class CScriptedDataFileInfo;

class IFile;

namespace red { class AbsolutePath; }

//---------------------------------------------------------------------------

/// Error reporter for validation
class IScriptDataErrorReporter
{
public:
	virtual ~IScriptDataErrorReporter() {};
	virtual void ValidationError( STATIC_CHECK_PRINTF_MSC const char* txt, ... ) = 0 ;
	virtual void BindingError( STATIC_CHECK_PRINTF_MSC const char* txt, ... ) = 0;
};

//---------------------------------------------------------------------------

/// Global holder for scripting meta data
class RED_REFLECTION_API CScriptedDataEnvironment
{
public:
	CScriptedDataEnvironment();
	~CScriptedDataEnvironment();

	/// Clear (reset)
	void Clear();

	/// Save to file
	Bool Save( const red::AbsolutePath& absoluteFilePath ) const;
	Bool Save( IFile& file ) const;

	/// Load from file
	Bool Load( const red::AbsolutePath& absoluteFilePath );
	Bool Load( IFile& file );

	Bool ValidateHeader( const red::AbsolutePath& absoluteFilePath );

	/// Get all objects (slow)
	void GetAllObjects( red::DynArray< const IScriptDataObject* >& allObjects, const Bool recursive ) const;

	/// Specific access types
	void GetAllEnums( red::DynArray< const CScriptedDataEnum* >& allEnums ) const;
	void GetAllClasses( red::DynArray< const CScriptedDataClass* >& allClasses ) const;
	void GetAllCastFunctions( red::DynArray< const CScriptedDataFunction* >& allFunctions ) const;
	void GetAllGlobalFunctions( red::DynArray< const CScriptedDataFunction* >& allFunctions ) const;
	void GetAllOperatorFunctions( red::DynArray< const CScriptedDataFunction* >& allFunctions ) const;
	void GetAllScriptFiles( red::DynArray< const CScriptedDataFileInfo* >& allFiles ) const;

	/// Find type
	IScriptDataObject* FindType( const CName name ) const;
	CScriptedDataClass* FindClass( const CName name ) const;
	CScriptedDataEnum* FindEnum( const CName name ) const;
	CScriptedDataBitfield* FindBitfield( const CName name ) const;
	CScriptedDataFunction* FindFunction( const CName name ) const;
	void EnumFunctionsFromFamily( const CName familyName, red::Map< CName, const CScriptedDataFunction* >& functions ) const;
	CScriptedDataNamedValue* FindEnumValue( CName enumName, const CName valueName ) const;
	CScriptedDataTypeRef* FindTypeRef( const CName name ) const; // only existing types
	CScriptedDataFileInfo* FindScriptFile( const Uint32 hash ) const;

	/// Add new global object
	CScriptedDataClass* AddClass( const CName name );
	CScriptedDataEnum* AddEnum( const CName name, const Uint32 size );
	CScriptedDataBitfield* AddBitfield( const CName name, const Uint32 size );
	CScriptedDataFunction* AddFunction( const CName functionName, CName familyName );
	CScriptedDataFileInfo* AddFileInfo(  const Uint32 hash );

	/// Add type reference to the library, reuses existing ones if possible to save memory
	CScriptedDataTypeRef* AddTypeRefNamed( const CName name ); // very simple named type Uint8, Bool, etc
	CScriptedDataTypeRef* AddTypeRefClass( const CScriptedDataClass* existingObject );
	CScriptedDataTypeRef* AddTypeRefEnum( const CScriptedDataEnum* existingObject );
	CScriptedDataTypeRef* AddTypeRefBitfield( const CScriptedDataBitfield* existingObject );
	CScriptedDataTypeRef* AddTypeRefDynArray( const CScriptedDataTypeRef* elementType );
	CScriptedDataTypeRef* AddTypeRefStaticArray( const CScriptedDataTypeRef* elementType, const Int32 count );
	CScriptedDataTypeRef* AddTypeRefHandle( const CScriptedDataTypeRef* elementType, Bool weak );
	CScriptedDataTypeRef* AddTypeRefReference( const CScriptedDataTypeRef* elementType );

	RED_INLINE void SetFileInfos( red::DynArray< CScriptedDataFileInfo* >& files ) { m_scriptFiles = files; }
	RED_INLINE void SetFileInfos( red::DynArray< CScriptedDataFileInfo* >&& files ) { m_scriptFiles = std::move( files ); }
	RED_INLINE void ExtractFileInfos( red::DynArray< CScriptedDataFileInfo* >& files ) { files = std::move( m_scriptFiles ); }

private:
	void CreateTypes( const red::DynArray< IScriptDataObject* >& objects );
	void CreateTypeRefs( const red::DynArray< IScriptDataObject* >& objects );
	void ResolveTypes( const red::DynArray< IScriptDataObject* >& objects );
	void ResolveFunctionsCode();
	void ResolveFunction( CScriptedDataFunction* const addedFunction, const CScriptedDataFunction* const referenceFunction );
	void ResolveClass( const CScriptedDataClass* const referenceClass );
	CScriptedDataTypeRef* FindTypeRefInternal( const CName name ) const;			// this version doesn't look up for "fallback" type name
	void AddTypeRefInternal( CScriptedDataTypeRef* typeRef, const CName name );

	CScriptedDataTypeRef* ResolveTypeRef( const CScriptedDataTypeRef* const typeref );

private:
	typedef red::DynArray< CScriptedDataFunction* >		TGlobalFunctions;
	typedef red::DynArray< CScriptedDataFileInfo* >		TScriptFiles;
	typedef red::DynArray< CScriptedDataEnum* >			TGlobalEnums;
	typedef red::DynArray< CScriptedDataBitfield* >		TGlobalBitfields;
	typedef red::DynArray< CScriptedDataClass* >		TGlobalClasses;
	typedef red::DynArray< CScriptedDataTypeRef* >		TGlobalTypeRefs;	

	typedef red::HashMap< CName, IScriptDataObject* >					TSymbolMap;
	typedef red::HashMap< CName, CScriptedDataFunction* >				TFunctionName;
	typedef red::HashMap< CName, CScriptedDataTypeRef* >				TTypeRefMap;		// runtime map

	typedef red::RWSpinLock Lock;

	TSymbolMap			m_symbolMap;
	TFunctionName		m_functionMap;
	TTypeRefMap			m_typeRefMap;
	
	TScriptFiles		m_scriptFiles;
	TGlobalFunctions	m_globalFunctions;
	TGlobalEnums		m_globalEnums;
	TGlobalBitfields	m_globalBitfields;
	TGlobalClasses		m_globalClasses;
	TGlobalTypeRefs		m_globalTypeRefs;

	mutable Lock		m_typesLock;
};
