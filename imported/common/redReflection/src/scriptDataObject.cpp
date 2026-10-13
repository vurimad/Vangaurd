/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptDataObject.h"
#include "scriptDataLoader.h"
#include "scriptDataSaver.h"
#include "scriptOpcodeTransformer.h"

using red::DynArray;

//---------------------------------------------------------------------------

IScriptDataObject::IScriptDataObject( const CName name )
	: m_name( name )
{
}


const ScriptDataSourceContext* IScriptDataObject::GetContext() const
{
#ifndef RED_CONFIGURATION_FINAL
	return m_context.Get();
#else
	return nullptr;
#endif

}

void IScriptDataObject::SetContext( red::UniquePtr< ScriptDataSourceContext >& context )
{
#ifndef RED_CONFIGURATION_FINAL
	m_context = std::move( context );
#endif
}


//----------------------------------------------------------------------

CScriptedDataFileInfo::CScriptedDataFileInfo()
	: IScriptDataObject( CName::NONE() )
	, m_fileCRC( 0 )
	, m_fileIndex( -1 )
	, m_hashedPath( 0 )
{
}

void CScriptedDataFileInfo::operator=(const CScriptedDataFileInfo& other)
{
	m_fileIndex = other.GetFileIndex();
	m_hashedPath = other.GetHash();
	m_fileCRC = other.GetCRC();
#if !defined( RED_PLATFORM_CONSOLE) || !defined( RED_CONFIGURATION_FINAL )
	m_relativePath = other.GetRelativePath();
#endif
}

CScriptedDataFileInfo::~CScriptedDataFileInfo() = default;

void CScriptedDataFileInfo::SetHash( Uint32 hash )
{
	m_hashedPath = hash;
}

void CScriptedDataFileInfo::SetCRC( Uint32 crc )
{
	m_fileCRC = crc;
}

void CScriptedDataFileInfo::SetRelativePath( const String& path )
{
#if !defined( RED_PLATFORM_CONSOLE) || !defined( RED_CONFIGURATION_FINAL )
	m_relativePath = path;
#endif
}

const String& CScriptedDataFileInfo::GetRelativePath() const 
{
#if !defined( RED_PLATFORM_CONSOLE) || !defined( RED_CONFIGURATION_FINAL )
	return m_relativePath; 
#else
	return String::EMPTY();
#endif
}

void CScriptedDataFileInfo::SetFileIndex( Int32 index )
{
	m_fileIndex = index;
}

void CScriptedDataFileInfo::Save( class IScriptDataSaver& saver ) const
{
	saver << m_fileIndex;
	saver << m_hashedPath;
	saver << m_fileCRC;
#if !defined( RED_PLATFORM_CONSOLE) || !defined( RED_CONFIGURATION_FINAL )
	saver << m_relativePath;
#endif
}

void CScriptedDataFileInfo::Load( class IScriptDataLoader& loader )
{
	loader >> m_fileIndex;
	loader >> m_hashedPath;
	loader >> m_fileCRC;
#if defined( RED_PLATFORM_CONSOLE) && defined( RED_CONFIGURATION_FINAL )
	// ctremblay: would be better to skip instead of dummy string.
	String path;
	loader >> path;
#else
	loader >> m_relativePath;
#endif
}

//---------------------------------------------------------------------------

CScriptedDataTypeRef::CScriptedDataTypeRef()
	: IScriptDataObject( CName::NONE() )
	, m_resolvedPtr( nullptr )
	, m_internal( nullptr )
	, m_arrayCount( 0 )
	, m_type( EType::Internal )
{}

CScriptedDataTypeRef::CScriptedDataTypeRef( const CName name )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_internal( nullptr )
	, m_arrayCount( 0 )
	, m_type( EType::Internal )
{}

CScriptedDataTypeRef::CScriptedDataTypeRef( const EType type, const CName name )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_internal( nullptr )
	, m_arrayCount( 0 )
	, m_type( type )
{}

CScriptedDataTypeRef::CScriptedDataTypeRef( const EType type, const CName name, const IScriptDataObject* internalRef, Int32 countForArray/*=-1*/ )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_internal( internalRef )
	, m_arrayCount( countForArray )
	, m_type( type )
{}

CScriptedDataTypeRef::~CScriptedDataTypeRef() = default;

void CScriptedDataTypeRef::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8)m_type;

	switch ( m_type )
	{
	case EType::StrongHandle:
	case EType::WeakHandle:
	case EType::StaticArray:
	case EType::DynArray:
	case EType::Reference:
		saver << m_internal;
		break;
	}

	if ( m_type == EType::StaticArray )
	{
		saver << m_arrayCount;
	}
}

const IScriptDataObject* CScriptedDataTypeRef::GetBase() const
{
	if ( IsArrayType() || IsHandleType() )
	{
		RED_FATAL_ASSERT( m_internal != nullptr, "Array type or handle type should have non-null internal type." );
		return m_internal;
	}
	if ( m_type == EType::Internal )
	{
		RED_FATAL_ASSERT( m_internal != nullptr, "Null internal type." );
		return m_internal->GetBase();
	}
	return nullptr;
}

void CScriptedDataTypeRef::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_type;

	switch ( m_type )
	{
	case EType::StrongHandle:
	case EType::WeakHandle:
	case EType::StaticArray:
	case EType::DynArray:
	case EType::Reference:
		loader >> m_internal;
		break;
	}

	if ( m_type == EType::StaticArray )
	{
		loader >> m_arrayCount;
	}
}

const String CScriptedDataTypeRef::ToString() const
{
	String ret;

	String internalName;
	if ( m_internal )
	{
		if ( m_internal->GetMetaType() == EMetaType::TypeRef )
		{
			internalName = static_cast< const CScriptedDataTypeRef* >( m_internal )->ToString();
		}
		else
		{
			internalName = m_internal->GetName().AsChar();
		}
	}

	switch ( m_type )
	{
	case EType::Named:
		ret = m_name.AsChar();
		break;

	case EType::Internal:
		ret = internalName;
		break;

	case EType::StrongHandle:
		ret += "ref<";
		ret += internalName;
		ret += ">";
		break;

	case EType::WeakHandle:
		ret += "wref<";
		ret += internalName;
		ret += ">";
		break;

	case EType::StaticArray:
		ret += internalName;
		ret += String::Printf( "[%d]", m_arrayCount );
		break;

	case EType::DynArray:
		ret += "array<";
		ret += internalName;
		ret += ">";
		break;

	case EType::Reference:
		ret += "script_ref<";
		ret += internalName;
		ret += ">";
		break;
	}

	return ret;
}
//---------------------------------------------------------------------------

CScriptedDataClass::CScriptedDataClass( const CName name )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_baseClass( nullptr )
	, m_properties( red::PoolScript() )
	, m_overridenProperties( red::PoolScript() ) 
	, m_functions( red::PoolScript() )
	, m_visibility( EVisibility::Public )
	, m_isImported( false )
	, m_isImportOnly( false )
	, m_isAbstract( false )
	, m_isFinal( false )
	, m_isStructure( false )
	, m_isTestOnly( false )
{
}

CScriptedDataClass::~CScriptedDataClass()
{
	red::alg::ClearPtr( m_functions );
	red::alg::ClearPtr( m_properties );
	red::alg::ClearPtr( m_overridenProperties );
}

void CScriptedDataClass::SetVisibility( EVisibility visibility )
{
	m_visibility = visibility;
}

void CScriptedDataClass::SetBaseClass( CScriptedDataClass* baseClass )
{
	m_baseClass = baseClass;
}

CScriptedDataFunction* CScriptedDataClass::AddFunction( CName functionName, CName familyName, EVisibility visibility )
{
	RED_FATAL_ASSERT( FindLocalFunction( functionName ) == nullptr, "Function already exists" );

	auto* func = RED_NEW( CScriptedDataFunction )( functionName, familyName, this, visibility );
	m_functions.PushBack( func );
	return func;
}

CScriptedDataProperty* CScriptedDataClass::AddProperty( CName propertyName, EVisibility visibility )
{
	RED_FATAL_ASSERT( FindProperty( propertyName ) == nullptr, "Property already exists" );

	auto* prop = RED_NEW( CScriptedDataProperty )( propertyName, this, visibility );
	m_properties.PushBack( prop );
	return prop;
}

CScriptedDataProperty* CScriptedDataClass::AddOverridenProperty( CName propertyName, EVisibility visibility )
{
	RED_FATAL_ASSERT( FindOverridenProperty( propertyName ) == nullptr, "Property already exists" );

	auto* prop = RED_NEW( CScriptedDataProperty )( propertyName, this, visibility );
	m_overridenProperties.PushBack( prop );
	return prop;
}

void CScriptedDataClass::GetAllFunctions( TFunctions& functions ) const
{
	// first get local functions
	functions.PushBack( m_functions );
	if ( m_baseClass != nullptr )
	{
		m_baseClass->GetAllFunctions( functions );
	}
}

void CScriptedDataClass::GetAllProperties( TProps& props ) const
{
	// first get properties of base class
	if ( m_baseClass != nullptr )
	{
		m_baseClass->GetAllProperties( props );
	}
	props.PushBack( m_properties );
}

CScriptedDataFunction* CScriptedDataClass::FindFunction( CName functionName ) const
{
	for ( CScriptedDataFunction* funcPtr : m_functions )
	{
		if ( funcPtr->GetName() == functionName )
		{
			return funcPtr;
		}
	}

	if ( m_baseClass )
	{
		return m_baseClass->FindFunction( functionName );
	}

	return nullptr;
}

void CScriptedDataClass::EnumFunctionsFromFamily( const CName familyName, red::Map< CName, const CScriptedDataFunction* >& functions ) const
{
	for ( CScriptedDataFunction* funcPtr : m_functions )
	{
		if ( funcPtr->GetFamilyName() == familyName )
		{
			// We need to use Insert instead of assignment, so that function from base class doesn't "hide" overriden one.
			functions.Insert( funcPtr->GetName(), funcPtr );
		}
	}

	if ( m_baseClass )
	{
		m_baseClass->EnumFunctionsFromFamily( familyName, functions );
	}
}

CScriptedDataFunction* CScriptedDataClass::FindLocalFunction( CName functionName ) const
{
	for ( CScriptedDataFunction* funcPtr : m_functions )
	{
		if ( funcPtr->GetName() == functionName )
		{
			return funcPtr;
		}
	}

	return nullptr;
}

CScriptedDataProperty* CScriptedDataClass::FindProperty( CName propertyName ) const
{
	for ( CScriptedDataProperty* propPtr : m_properties )
	{
		if ( propPtr->GetName() == propertyName )
		{
			return propPtr;
		}
	}

	if ( m_baseClass )
	{
		return m_baseClass->FindProperty( propertyName );
	}

	return nullptr;
}

CScriptedDataProperty* CScriptedDataClass::FindOverridenProperty( CName propertyName ) const
{
	for ( CScriptedDataProperty* propPtr : m_overridenProperties )
	{
		if ( propPtr->GetName() == propertyName )
		{
			return propPtr;
		}
	}

	return nullptr;
}

CScriptedDataProperty* CScriptedDataClass::FindLocalProperty( CName propertyName ) const
{
	for ( CScriptedDataProperty* propPtr : m_properties )
	{
		if ( propPtr->GetName() == propertyName )
		{
			return propPtr;
		}
	}

	return nullptr;
}

bool CScriptedDataClass::IsA( const CScriptedDataClass* baseClass ) const
{
	const CScriptedDataClass* testClass = this;
	while ( testClass )
	{
		if ( testClass == baseClass )
			return true;

		testClass = testClass->GetBaseClass();
	}

	return (baseClass == nullptr);	
}

void CScriptedDataClass::SetAbstract( const Bool isAbstrct )
{
	m_isAbstract = isAbstrct;
}

void CScriptedDataClass::SetFinal( const Bool isFinal )
{
	m_isFinal = isFinal;
}

void CScriptedDataClass::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataClass::SetImportOnly( const Bool isImportOnly )
{
	m_isImportOnly = isImportOnly;
}

void CScriptedDataClass::SetStructure( const Bool isStructure )
{
	m_isStructure = isStructure;
}

void CScriptedDataClass::SetTestOnly( const Bool isTestOnly )
{
	m_isTestOnly = isTestOnly;
}

void CScriptedDataClass::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8&)m_visibility;

	// flags
	Uint16 flags = 0;
	flags |= m_isImported			? SDCF_IsImported : 0;
	flags |= m_isImportOnly			? SDCF_IsImportOnly : 0;
	flags |= m_isAbstract			? SDCF_IsAbstract : 0;
	flags |= m_isFinal				? SDCF_IsFinal : 0;
	flags |= m_isStructure			? SDCF_IsStructure : 0;
	flags |= m_isTestOnly			? SDCF_IsTestOnly : 0;
	flags |= !m_functions.Empty()	? SDCF_HasFunctions : 0;
	flags |= !m_properties.Empty()	? SDCF_HasProps : 0;
	flags |= !m_overridenProperties.Empty()	? SDCF_HasOverridenProps : 0;

	saver << flags;
	saver << m_baseClass;

	// sub objects
	if ( !m_functions.Empty() )
		saver << m_functions;

	if ( !m_properties.Empty() )
		saver << m_properties;

	if ( !m_overridenProperties.Empty() )
		saver << m_overridenProperties;
}

void CScriptedDataClass::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_visibility;

	Uint16 flags = 0;
	loader >> flags;

	// flags
	m_isImported	= !!( flags & SDCF_IsImported );
	m_isImportOnly	= !!( flags & SDCF_IsImportOnly );
	m_isAbstract	= !!( flags & SDCF_IsAbstract );
	m_isFinal		= !!( flags & SDCF_IsFinal );
	m_isStructure	= !!( flags & SDCF_IsStructure );
	m_isTestOnly	= !!( flags & SDCF_IsTestOnly );

	loader >> m_baseClass;

	// sub objects
	if ( flags & SDCF_HasFunctions )
		loader >> m_functions;

	if ( flags & SDCF_HasProps )
		loader >> m_properties;

	if ( flags & SDCF_HasOverridenProps )
		loader >> m_overridenProperties;

}

CScriptedDataClass& CScriptedDataClass::operator=( const CScriptedDataClass& other )
{
	if ( this != &other )
	{
		m_visibility = other.m_visibility;
		m_isImported = other.m_isImported;
		m_isImportOnly = other.m_isImportOnly;
		m_isAbstract = other.m_isAbstract;
		m_isFinal = other.m_isFinal;
		m_isStructure = other.m_isStructure;
		m_isTestOnly = other.m_isTestOnly;

		m_functions.Clear();
		for ( const auto& func : other.m_functions )
		{
			if ( CScriptedDataFunction* const newFunc = AddFunction( func->GetName(), func->GetFamilyName(), func->GetVisibility() ) )
			{
				*newFunc = *func;
			}
		}

		m_properties.Clear();
		for ( const auto& prop : other.m_properties )
		{
			if ( CScriptedDataProperty* const newProp = AddProperty( prop->GetName(), prop->GetVisibility() ) )
			{
				*newProp = *prop;
			}
		}

		m_overridenProperties.Clear();
		for ( const auto& prop : other.m_overridenProperties )
		{
			if ( CScriptedDataProperty* const newProp = AddOverridenProperty( prop->GetName(), prop->GetVisibility() ) )
			{
				*newProp = *prop;
			}
		}
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataFunction::ReturnValue::ReturnValue()
	: m_type( nullptr )
	, m_isConst( false )
{}

CScriptedDataFunction::ReturnValue& CScriptedDataFunction::ReturnValue::operator=( const ReturnValue& other )
{
	if ( &other != this )
	{
		m_type = other.m_type;
		m_isConst = other.m_isConst;
	}
	return *this;
}

void CScriptedDataFunction::ReturnValue::Save( class IScriptDataSaver& saver ) const
{
	saver << m_type;

	Uint8 flags = 0;
	flags |= m_isConst ? SDFRVF_IsConst : 0;
	saver << flags;
}

void CScriptedDataFunction::ReturnValue::Load( class IScriptDataLoader& loader )
{
	loader >> m_type;

	Uint8 flags = 0;
	loader >> flags;
	m_isConst = !!( flags & SDFRVF_IsConst );
}

//---------------------------------------------------------------------------

CScriptedDataNamedValue::CScriptedDataNamedValue( const CName name, IScriptDataObject* parent )
	: IScriptDataObject( name )
	, m_resolvedValue( 0 )
	, m_parent( parent )
	, m_value( 0 )
	, m_isImported( false )
{
}

void CScriptedDataNamedValue::Save( class IScriptDataSaver& saver ) const
{
	saver << m_value;
}

void CScriptedDataNamedValue::Load( class IScriptDataLoader& loader )
{
	loader >> m_value;
}

void CScriptedDataNamedValue::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataNamedValue::SetValue( const Int64 value )
{
	m_value = value;
}

//---------------------------------------------------------------------------

CScriptedDataEnum::CScriptedDataEnum( const CName name )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_values( red::PoolScript() )
	, m_size( 4 )
	, m_isImported( false )
	, m_visibility( EVisibility::Public )
{
}

CScriptedDataEnum::~CScriptedDataEnum()
{
	red::alg::ClearPtr( m_values );
}

void CScriptedDataEnum::SetSize( const Uint8 size )
{
	RED_FATAL_ASSERT( size == 1 || size == 2 || size == 4 || size == 8, "Invalid enum size" );
	m_size = size;
}

CScriptedDataNamedValue* CScriptedDataEnum::AddValue( const CName name, Int64 value )
{
	for ( auto* ptr : m_values )
	{
		if ( ptr->GetName() == name )
		{
			ptr->SetValue( value );
			return ptr;
		}
	}

	CScriptedDataNamedValue* val = RED_NEW( CScriptedDataNamedValue )( name, this );
	val->SetImported( m_isImported );
	val->SetValue( value );
	m_values.PushBack( val );

	return val;
}

CScriptedDataNamedValue* CScriptedDataEnum::FindValue( const CName name ) const
{
	for ( auto* ptr : m_values )
	{
		if ( ptr->GetName() == name )
		{
			return ptr;
		}
	}

	// not found
	return nullptr;
}

void CScriptedDataEnum::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataEnum::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8&)m_visibility;
	saver << m_size;
	saver << m_values;
	saver << m_isImported;
}

void CScriptedDataEnum::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_visibility;
	loader >> m_size;
	loader >> m_values;
	loader >> m_isImported;
}

CScriptedDataEnum& CScriptedDataEnum::operator=( const CScriptedDataEnum& other )
{
	if ( this != &other )
	{
		m_visibility = other.m_visibility;
		m_size = other.m_size;
		m_isImported = other.m_isImported;

		m_values.Clear();
		for ( const auto& value: other.m_values )
		{
			AddValue( value->GetName(), value->GetValue() );
		}
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataBitfield::CScriptedDataBitfield( const CName name )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_size( 4 )
	, m_isImported( false )
	, m_visibility( EVisibility::Public )
{
}

CScriptedDataBitfield::~CScriptedDataBitfield()
{
	red::alg::ClearPtr( m_values );
}

void CScriptedDataBitfield::SetSize( const Uint8 size )
{
	RED_FATAL_ASSERT( size == 1 || size == 2 || size == 4 || size == 8, "Invalid bitfield size" );
	m_size = size;
}

void CScriptedDataBitfield::AddBit( const CName name, Uint64 value )
{
	for ( Uint32 i=0; i<m_values.Size(); ++i )
	{
		if ( m_values[i]->GetName() == name )
		{
			m_values[i]->SetValue( value );
			return;
		}
	}

	CScriptedDataNamedValue* val = RED_NEW( CScriptedDataNamedValue )( name, this );
	val->SetImported( m_isImported );
	val->SetValue( value );
	m_values.PushBack( val );
}

void CScriptedDataBitfield::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataBitfield::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8&)m_visibility;
	saver << m_size;
	saver << m_values;
	saver << m_isImported;
}

void CScriptedDataBitfield::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_visibility;
	loader >> m_size;
	loader >> m_values;
	loader >> m_isImported;
}

CScriptedDataBitfield& CScriptedDataBitfield::operator=( const CScriptedDataBitfield& other )
{
	if ( this != &other )
	{
		m_visibility = other.m_visibility;
		m_size = other.m_size;
		m_isImported = other.m_isImported;
		
		m_values.Clear();
		for ( const auto& value : other.m_values )
		{
			AddBit( value->GetName(), value->GetValue() );
		}
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataProperty::CScriptedDataProperty( const CName name, IScriptDataObject* parent, EVisibility visibility )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_parent( parent )
	, m_isImported( false )
	, m_isEditable( false )
	, m_isInlined( false )
	, m_isConst( false )
	, m_isReplicated( false )
	, m_isInstanceEditable( false )
	, m_isPersistent( false )
	, m_isTestOnly( false )
	, m_isBrowsable( true )
	, m_defaultValues( red::PoolScript() )
	, m_visibility( visibility )
	, m_attributes( red::PoolScript() )
	, m_type( nullptr )
{
}

void CScriptedDataProperty::SetType( const CScriptedDataTypeRef* typeRef )
{
	m_type = typeRef;
}

void CScriptedDataProperty::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataProperty::SetTestOnly( const Bool isTestOnly )
{
	m_isTestOnly = isTestOnly;
}

void CScriptedDataProperty::SetBrowsable( const Bool isBrowsable )
{
	m_isBrowsable = isBrowsable;
}

void CScriptedDataProperty::SetInlined( const Bool isInlined )
{
	m_isInlined = isInlined;
}

void CScriptedDataProperty::SetEditable( const Bool isEditable )
{
	m_isEditable = isEditable;
}

void CScriptedDataProperty::SetInstanceEditable( const Bool isEditable )
{
	m_isInstanceEditable = isEditable;
}

void CScriptedDataProperty::SetConst( const Bool isConst )
{
	m_isConst = isConst;
}

void CScriptedDataProperty::SetReplicated( const Bool isReplicated )
{
	m_isReplicated = isReplicated;
}

void CScriptedDataProperty::SetPersistent( const Bool isPersistent )
{
	m_isPersistent = isPersistent;
}

void CScriptedDataProperty::SetHint( const String& hint )
{
#ifndef RED_CONFIGURATION_FINAL
	m_hint = hint;
#endif
}

const String& CScriptedDataProperty::GetHint() const
{
#ifndef RED_CONFIGURATION_FINAL
	return m_hint;
#else
	return String::EMPTY();
#endif
}

void CScriptedDataProperty::SetDefaultValue( const String& typeName, const String& value )
{
	m_defaultValues[ typeName ] = value;
}

void CScriptedDataProperty::SetAttribute( const red::StringView& name, const red::StringView& value )
{
	m_attributes[ RED_NAME( name ) ] = { value.Data(), value.Length() };
}

const red::String& CScriptedDataProperty::GetAttributeValue( const char* name ) const
{
	auto it = m_attributes.Find( RED_NAME_NOREG( name ) );
	return it != m_attributes.End() ? it.Value() : red::String::EMPTY();
}

void CScriptedDataProperty::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8&)m_visibility;
	saver << m_type;

	Uint16 flags = 0;
	flags |= m_isImported		? SDPF_IsImported : 0;
	flags |= m_isEditable		? SDPF_IsEditable : 0;
	flags |= m_isInlined		? SDPF_IsInlined : 0;
	flags |= m_isConst			? SDPF_IsConst : 0;
	flags |= m_isReplicated		? SDPF_IsReplicated : 0;
#ifndef RED_CONFIGURATION_FINAL
	flags |= !m_hint.Empty()	? SDPF_HasHint : 0;
#endif
	flags |= m_isInstanceEditable ? SDPF_IsInstanceEditable : 0;
	flags |= m_isPersistent		? SPDF_IsPersistent : 0;
	flags |= m_isTestOnly		? SPDF_IsTestOnly : 0;
	flags |= m_isBrowsable		? SPDF_IsBrowsable : 0;
	flags |= !m_defaultValues.Empty() ? SDPF_HasDefaultValue : 0;
	saver << flags;

#ifndef RED_CONFIGURATION_FINAL
	if ( !m_hint.Empty() )
		saver << m_hint;
#endif

	saver << m_attributes.Size();
	for ( auto& attribute : m_attributes )
	{
		saver << attribute.Key().AsStringView().ToString();
		saver << attribute.Value();
	}

	saver << m_defaultValues.Size();
	for ( auto& defaultValue : m_defaultValues )
	{
		saver << defaultValue.Key();
		saver << defaultValue.Value();
	}
}

void CScriptedDataProperty::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_visibility;
	loader >> m_type;

	Uint16 flags = 0;
	loader >> flags;
	
	m_isImported = !!( flags & SDPF_IsImported );
	m_isEditable = !!( flags & SDPF_IsEditable );
	m_isInlined = !!( flags & SDPF_IsInlined );
	m_isConst = !!( flags & SDPF_IsConst );
	m_isReplicated = !!( flags & SDPF_IsReplicated );
	m_isInstanceEditable = !!( flags & SDPF_IsInstanceEditable );
	m_isPersistent = !!( flags & SPDF_IsPersistent );
	m_isTestOnly = !!( flags & SPDF_IsTestOnly );
	m_isBrowsable = !!( flags & SPDF_IsBrowsable );

	if ( flags & SDPF_HasHint )
	{
#ifndef RED_CONFIGURATION_FINAL
		loader >> m_hint;
#else
		String hint;
		loader >> hint;
#endif
	}
		

	Uint32 attributesSize = 0;
	loader >> attributesSize;
	m_attributes.Reserve( attributesSize );

	for ( Uint32 i = 0; i < attributesSize; ++i )
	{
		String name;
		loader >> name;
		String value;
		loader >> value;
		m_attributes[ RED_NAME( name ) ] = value;
	}

	Uint32 defaultValuesSize = 0;
	loader >> defaultValuesSize;
	m_defaultValues.Reserve( defaultValuesSize );

	for ( Uint32 i = 0; i < defaultValuesSize; ++i )
	{
		String typeName;
		loader >> typeName;
		String defaultValue;
		loader >> defaultValue;
		m_defaultValues[ typeName ] = defaultValue;
	}
}

CScriptedDataProperty& CScriptedDataProperty::operator=( const CScriptedDataProperty& other )
{
	if ( this != &other )
	{
		m_visibility = other.m_visibility;
		m_type = other.m_type;
		m_isImported = other.m_isImported;
		m_isEditable = other.m_isEditable;
		m_isReplicated = other.m_isReplicated;
		m_isInlined = other.m_isInlined;
		m_isConst = other.m_isConst;
		m_isInstanceEditable = other.m_isInstanceEditable;
		m_isPersistent = other.m_isPersistent;
		m_isTestOnly = other.m_isTestOnly;
		m_isBrowsable = other.m_isBrowsable;
#ifndef RED_CONFIGURATION_FINAL
		m_hint = other.m_hint;
#endif
		m_attributes = other.m_attributes;
		m_defaultValues = other.m_defaultValues;
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataFunctionParam::CScriptedDataFunctionParam( const CName name, CScriptedDataFunction* func )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_function( func )
	, m_type( nullptr )
	, m_isOptional( false )
	, m_isRef( false )
	, m_isSkipped( false )
	, m_isConst( false )
{
}

void CScriptedDataFunctionParam::SetType( const CScriptedDataTypeRef* type )
{
	m_type = type;
}

void CScriptedDataFunctionParam::SetOptional( const Bool isOptional )
{
	m_isOptional = isOptional;
}

void CScriptedDataFunctionParam::SetReference( const Bool isReference )
{
	m_isRef = isReference;
}

void CScriptedDataFunctionParam::SetSkipped( const Bool isSkipped )
{
	m_isSkipped = isSkipped;
}

void CScriptedDataFunctionParam::SetConst( const Bool isConst )
{
	m_isConst = isConst;
}

void CScriptedDataFunctionParam::Save( class IScriptDataSaver& saver ) const
{
	saver << m_type;

	Uint8 flags = 0;
	flags |= m_isOptional	? SDFPF_IsOptional : 0;
	flags |= m_isRef		? SDFPF_IsRef : 0;
	flags |= m_isSkipped	? SDFPF_IsSkipped : 0;
	flags |= m_isConst		? SDFPF_IsConst : 0;
	saver << flags;
}

void CScriptedDataFunctionParam::Load( class IScriptDataLoader& loader )
{
	loader >> m_type;

	Uint8 flags = 0;
	loader >> flags;

	m_isOptional	= !!( flags & SDFPF_IsOptional );
	m_isRef			= !!( flags & SDFPF_IsRef );
	m_isSkipped		= !!( flags & SDFPF_IsSkipped );
	m_isConst		= !!( flags & SDFPF_IsConst );
}

CScriptedDataFunctionParam& CScriptedDataFunctionParam::operator=( const CScriptedDataFunctionParam& other )
{
	if ( this != &other )
	{
		m_type = other.m_type;
		m_isOptional = other.m_isOptional;
		m_isRef = other.m_isRef;
		m_isSkipped = other.m_isSkipped;
		m_isConst = other.m_isConst;
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataFunctionLocal::CScriptedDataFunctionLocal( const CName name, CScriptedDataFunction* func )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_function( func )
	, m_type( nullptr )
	, m_isConst( false )
{
}

void CScriptedDataFunctionLocal::SetType( const CScriptedDataTypeRef* type )
{
	m_type = type;
}

void CScriptedDataFunctionLocal::SetConst( const Bool isConst )
{
	m_isConst = isConst;
}

void CScriptedDataFunctionLocal::Save( class IScriptDataSaver& saver ) const
{
	saver << m_type;

	Uint8 flags = 0;
	flags |= m_isConst ? SDFLF_IsConst : 0;
	saver << flags;
}

void CScriptedDataFunctionLocal::Load( class IScriptDataLoader& loader )
{
	loader >> m_type;

	Uint8 flags = 0;
	loader >> flags;
	m_isConst = !!( flags & SDFLF_IsConst );
}

CScriptedDataFunctionLocal& CScriptedDataFunctionLocal::operator=( const CScriptedDataFunctionLocal& other )
{
	if ( this != &other )
	{
		m_type = other.m_type;
		m_isConst = other.m_isConst;
	}

	return *this;
}

//---------------------------------------------------------------------------

CScriptedDataFunction::CScriptedDataFunction( const CName name, const CName familyName, IScriptDataObject* parent, EVisibility visibility )
	: IScriptDataObject( name )
	, m_resolvedPtr( nullptr )
	, m_parent( parent )
	, m_familyName( familyName )
	, m_params( red::PoolScript() )
	, m_locals( red::PoolScript() )
	, m_superFunc( nullptr )
	, m_isStatic( false )
	, m_isExec( false )
	, m_isTimer( false )
	, m_isFinal( false )
	, m_isImported( false )
	, m_isImportedOverride( false )
	, m_isEvent( false )
	, m_isOperator( false )
	, m_isCast( false )
	, m_isCastImplicit( false )
	, m_isMulticast( false )
	, m_isHost( false )
	, m_isClient( false )
	, m_isReliable( false )
	, m_isConst( false )
	, m_isThreadSafe( false )
	, m_isQuest( false )
	, m_isTestOnly( false )
	, m_castCost( 0 )
	, m_visibility( visibility )
	, m_rawCode( red::PoolScript() )
	, m_sourceFileInfo( nullptr )
	, m_sourceLine( 0 )
{}

CScriptedDataFunction::~CScriptedDataFunction() = default;

CScriptedDataFunctionParam* CScriptedDataFunction::AddParam( const CName name )
{
	RED_FATAL_ASSERT( nullptr == FindParam( name ), "Already defined as param" );
	RED_FATAL_ASSERT( nullptr == FindLocal( name ), "Already defined as local" );

	CScriptedDataFunctionParam* ret = RED_NEW( CScriptedDataFunctionParam )( name, this );
	m_params.PushBack( ret );
	return ret;
}

CScriptedDataFunctionLocal* CScriptedDataFunction::AddLocal( const CName name )
{
	RED_FATAL_ASSERT( nullptr == FindParam( name ), "Already defined as param" );
	RED_FATAL_ASSERT( nullptr == FindLocal( name ), "Already defined as local" );

	CScriptedDataFunctionLocal* ret = RED_NEW( CScriptedDataFunctionLocal )( name, this );
	m_locals.PushBack( ret );
	return ret;
}

CScriptedDataFunctionParam* CScriptedDataFunction::FindParam( const CName name ) const
{
	for ( auto* ptr : m_params )
	{
		if ( ptr->GetName() == name )
		{
			return ptr;
		}
	}

	return nullptr;
}

CScriptedDataFunctionLocal* CScriptedDataFunction::FindLocal( const CName name ) const
{
	for ( auto* ptr : m_locals )
	{
		if ( ptr->GetName() == name )
		{
			return ptr;
		}
	}

	return nullptr;
}

void CScriptedDataFunction::SetStatic( const Bool isStatic )
{
	m_isStatic = isStatic;
}

void CScriptedDataFunction::SetExec( const Bool isExec )
{
	m_isExec = isExec;
}

void CScriptedDataFunction::SetTimer( const Bool isTimer )
{
	m_isTimer = isTimer;
}

void CScriptedDataFunction::SetFinal( const Bool isFinal )
{
	m_isFinal = isFinal;
}

void CScriptedDataFunction::SetImported( const Bool isImported )
{
	m_isImported = isImported;
}

void CScriptedDataFunction::SetImportedOverride( const Bool isImportedOverride )
{
	m_isImportedOverride = isImportedOverride;
}

void CScriptedDataFunction::SetEvent( const Bool isEvent )
{
	m_isEvent = isEvent;
}

void CScriptedDataFunction::SetOperator( const Bool isOperator )
{
	m_isOperator = isOperator;
}

void CScriptedDataFunction::SetOperationName( const CName name )
{
	m_operationName = name;
}

void CScriptedDataFunction::SetCast( const Bool isCast )
{
	m_isCast = isCast;
}

void CScriptedDataFunction::SetCastImplicit( const Bool isCastImplicit )
{
	m_isCastImplicit = isCastImplicit;
}

void CScriptedDataFunction::SetCastCost( const Uint8 castCost )
{
	m_castCost = castCost;
}

void CScriptedDataFunction::SetMulticast( const Bool isMulticast )
{
	m_isMulticast = isMulticast;
}

void CScriptedDataFunction::SetServer( const Bool isServer )
{
	m_isHost = isServer;
}

void CScriptedDataFunction::SetClient( const Bool isClient )
{
	m_isClient = isClient;
}

void CScriptedDataFunction::SetReliable( const Bool isReliable )
{
	m_isReliable = isReliable;
}

void CScriptedDataFunction::SetConst( const Bool isConst )
{
	m_isConst = isConst;
}

void CScriptedDataFunction::SetThreadSafe( const Bool isThreadSafe )
{
	m_isThreadSafe = isThreadSafe;
}

void CScriptedDataFunction::SetQuest( const Bool isQuest )
{
	m_isQuest = isQuest;
}

void CScriptedDataFunction::SetTestOnly( const Bool isTestOnly )
{
	m_isTestOnly = isTestOnly;
}

void CScriptedDataFunction::SetSuperFunction( const CScriptedDataFunction* superFunc )
{
	m_superFunc = superFunc;
}

void CScriptedDataFunction::SetRawCode( const void* code, const Uint32 codeSize )
{
	RED_FATAL_ASSERT( m_rawCode.Empty(), "Code already set" );
	RED_FATAL_ASSERT( code != nullptr && codeSize != 0, "No point calling with empty code" );

	m_rawCode.Resize( codeSize );
	red::Memcpy( m_rawCode.Data(), code, codeSize );
}

void CScriptedDataFunction::SetFileInfo( const CScriptedDataFileInfo* fileInfo )
{
	// TODO: remove theese assertions
	RED_FATAL_ASSERT( fileInfo != nullptr, "File shouldn't be null." );
	RED_FATAL_ASSERT( m_sourceFileInfo == nullptr, "Trying to change existing file info." );

	m_sourceFileInfo = fileInfo;
}

CName CScriptedDataFunction::CreateFamilyName( CName functionName )
{
	red::StringView nameView = functionName.AsStringView();
	Uint32 pos = nameView.Find( ';' );
	if ( pos != red::StringView::npos )
	{
		return RED_NAME( nameView.SubView( 0, pos ) );
	}
	return functionName;
}

red::String CScriptedDataFunction::CreateDebugName() const
{
	const Uint32 MAX_NAME_LENGTH = 256;
	char buffer[ MAX_NAME_LENGTH ];
	const char* nameStr = m_familyName.AsChar();
	const Uint32 paramsNum = m_params.Size();
	const Uint32 nameLength = static_cast< Uint32 >( red::Strlen( nameStr ) );
	red::Memcpy( buffer, nameStr, nameLength );
	Uint32 index = nameLength;
	buffer[ index++ ] = '(';
	for ( Uint32 i = 0; i < paramsNum; i++ )
	{
		const String typeName = m_params[ i ]->GetType()->ToString();
		red::Memcpy( buffer + index, typeName.AsChar(), typeName.Length() );
		index += typeName.Length();
		if ( i < paramsNum - 1 )
		{
			buffer[ index++ ] = ',';
		}
	}

	buffer[ index++ ] =')';
	buffer[ index ] = 0;
	RED_FATAL_ASSERT( index < MAX_NAME_LENGTH );
	return red::String( buffer );
}

void CScriptedDataFunction::UndecorateName()
{
	m_name = m_familyName;
}

void CScriptedDataFunction::SetAttribute( const red::StringView& name, const red::StringView& value )
{
	m_attributes[ RED_NAME( name ) ] = { value.Data(), value.Length() };
}

const red::String& CScriptedDataFunction::GetAttributeValue( const char* name ) const
{
	auto it = m_attributes.Find( RED_NAME( red::StringView( name ) ) );
	return it != m_attributes.End() ? it.Value() : red::String::EMPTY();
}

namespace Helper
{
	// Transforms code from memory representation to disk representation
	class CodeMemoryToDisk : public CScriptOpcodeTransformer
	{
	public:
		CodeMemoryToDisk( const void* data, const Uint32 size, IScriptDataSaver& saver )
			: m_pos( 0 )
			, m_size( size )
			, m_data( static_cast< const Uint8* >( data ) )
			, m_saver( &saver )
		{}

		virtual Bool EndOfStream() const override
		{
			return m_pos >= m_size;
		}

		virtual Uint8 ProcessOpcode() override
		{
			const auto op = Read<Uint8>();
			Write( op );
			return op;
		}

		virtual void ProcessBreakpoint() override
		{
			// Line
			ProcessUint16();

			// Line position
			ProcessUint32();

			// "Column"
			ProcessUint16();

			// Length
			ProcessUint16();

			// Breakpoint toggle
			ProcessUint8();

			// Condition
			Write< intptr_t >( Read< intptr_t >() );
		}

		virtual void ProcessStartProfile() override
		{
			// Function name
			ProcessString();

			// Instrumentation object
			Write< intptr_t >( Read< intptr_t >() );
			
			// Function was explicitly marked for profile
			Write< Int8 >( Read< Int8 >() );
		}

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

			for ( Uint32 i=0; i<len; ++i )
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

		virtual void ProcessTypeRef() override
		{
			Write( Read< const CScriptedDataTypeRef* >() );
		}

		virtual void ProcessEnum() override
		{
			Write(Read< const CScriptedDataEnum* >());
		}

		virtual void ProcessPointer() override
		{
			Write(Read< const CScriptedDataClass* >());
		}

		virtual void ProcessPropertyRef() override
		{
			Write( Read< const CScriptedDataProperty* >() );
		}

		virtual void ProcessLocalPropertyRef() override
		{
			Write(Read< const CScriptedDataFunctionLocal * >());
		}

		virtual void ProcessParamPropertyRef() override
		{
			Write(Read< const CScriptedDataFunctionParam * >());
		}

		virtual void ProcessFunctionRef() override
		{
			Write( Read< const CScriptedDataFunction* >() );
		}

		virtual void ProcessNamedValueRef() override
		{
			Write( Read< const CScriptedDataNamedValue* >() );
		}

	private:
		// input
		Uint32			m_pos;
		Uint32			m_size;
		const Uint8*	m_data;

		// read data
		template< typename T >
		RED_INLINE T Read()
		{
			RED_FATAL_ASSERT( m_pos + sizeof(T) <= m_size, "Reading past stream end" );
			const T ret = *(const T*)( m_data + m_pos );
			m_pos += sizeof(T);
			return ret;
		}

		// write data
		template< typename T >
		RED_INLINE void Write( const T& data )
		{
			(*m_saver) << data;
		}

		// output
		IScriptDataSaver*	m_saver;
	};

	// Transforms code from disk representation to memory representation
	class CodeDiskToMemory : public CScriptOpcodeTransformer
	{
	public:
		CodeDiskToMemory( void* data, const Uint32 size, IScriptDataLoader& loader )
			: m_loader( &loader )
			, m_pos( 0 )
			, m_size( size )
			, m_data( static_cast< Uint8* >( data ) )
		{}

		virtual Bool EndOfStream() const override
		{
			return m_pos >= m_size;
		}

		virtual Uint8 ProcessOpcode() override
		{
			const auto op = Read<Uint8>();
			Write( op );
			return op;
		}

		virtual void ProcessBreakpoint() override
		{
			// Line
			ProcessUint16();

			// Line position
			ProcessUint32();

			// "Column"
			ProcessUint16();

			// Length
			ProcessUint16();

			// Breakpoint toggle
			ProcessUint8();

			// Condition
			Write< intptr_t >( Read< intptr_t >() );
		}

		virtual void ProcessStartProfile() override
		{
			// Function name
			ProcessString();

			// Instrumentation object
			Write< intptr_t >( Read< intptr_t >() );

			// Function was explicitly marked for profile
			Write< Int8 >( Read< Int8 >() );
		}

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

			for ( Uint32 i=0; i<len; ++i )
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

		virtual void ProcessTypeRef() override
		{
			Write( Read< const CScriptedDataTypeRef* >() );
		}

		virtual void ProcessEnum() override
		{
			Write(Read< const CScriptedDataEnum* >());
		}

		virtual void ProcessPointer() override
		{
			Write(Read< const CScriptedDataClass* >());
		}

		virtual void ProcessPropertyRef() override
		{
			Write( Read< const CScriptedDataProperty* >() );
		}

		virtual void ProcessLocalPropertyRef() override
		{
			Write(Read< const CScriptedDataFunctionLocal* >());
		}

		virtual void ProcessParamPropertyRef() override
		{
			Write(Read< const CScriptedDataFunctionParam* >());
		}

		virtual void ProcessFunctionRef() override
		{
			Write( Read< const CScriptedDataFunction* >() );
		}

		virtual void ProcessNamedValueRef() override
		{
			Write( Read< const CScriptedDataNamedValue* >() );
		}

	private:
		// input
		IScriptDataLoader*	m_loader;

		// output
		Uint32			m_pos;
		Uint32			m_size;
		Uint8*			m_data;

		// read data
		template< typename T >
		RED_INLINE T Read()
		{
			T ret;
			(*m_loader) >> ret;
			return ret;
		}

		// write data
		template< typename T >
		RED_INLINE void Write( const T& data )
		{
			RED_FATAL_ASSERT( m_pos + sizeof(T) <= m_size, "Reading past stream end" );
			*(T*)( m_data + m_pos ) = data;
			m_pos += sizeof(T);
		}
	};
}

void CScriptedDataFunction::Save( class IScriptDataSaver& saver ) const
{
	saver << (Uint8&)m_visibility;

	const Bool hasReturnType = ( m_returnValue.GetType() != nullptr );
	Uint32 flags = 0;
	flags |= m_isStatic					? SDFF_IsStatic : 0;
	flags |= m_isExec					? SDFF_IsExec : 0;
	flags |= m_isTimer					? SDFF_IsTimer : 0;
	flags |= m_isFinal					? SDFF_IsFinal : 0;
	flags |= m_isImported				? SDFF_IsImported : 0;
	flags |= m_isEvent					? SDFF_IsEvent : 0;
	flags |= m_isOperator				? SDFF_IsOperator : 0;
	flags |= hasReturnType				? SDFF_HasRetType : 0;
	flags |= ( m_superFunc != nullptr )	? SDFF_HasSuperFunc : 0;
	flags |= !m_params.Empty()			? SDFF_HasParams : 0;
	flags |= !m_locals.Empty()			? SDFF_HasLocals : 0;
	flags |= !m_rawCode.Empty()			? SDFF_HasRawCode : 0;
	flags |= m_isCast					? SDFF_IsCast : 0;
	flags |= m_isCastImplicit			? SDFF_IsCastImplicit : 0;
	flags |= m_isMulticast				? SDFF_IsMulticast : 0;
	flags |= m_isHost					? SDFF_IsServer : 0;
	flags |= m_isClient					? SDFF_IsClient : 0;
	flags |= m_isReliable				? SDFF_IsReliable : 0;
	flags |= m_isConst					? SDFF_IsConst : 0;
	flags |= m_isThreadSafe				? SDFF_IsThreadSafe : 0;
	flags |= m_isQuest					? SDFF_IsQuest : 0;
	flags |= m_isTestOnly				? SDFF_IsTestOnly : 0;
	saver << flags;

	// Save script defined function debug info
	if ( !m_isImported )
	{
		saver << m_sourceFileInfo;
		saver << m_sourceLine;
	}
	
	if ( hasReturnType )
		m_returnValue.Save( saver );

	if ( m_superFunc != nullptr )
		saver << m_superFunc;

	if ( !m_params.Empty() )
		saver << m_params;

	if ( !m_locals.Empty() )
		saver << m_locals;

	if ( m_isOperator )
		saver << m_operationName;

	if ( m_isCast )
		saver << m_castCost;

	if ( !m_rawCode.Empty() )
	{
		const Uint32 size = m_rawCode.Size();
		saver << size;

		// transform code that we have in memory into savable representation
		Helper::CodeMemoryToDisk codeTransformer( m_rawCode.Data(), (Uint32)m_rawCode.DataSize(), saver );
		codeTransformer.PerformTransform();
	}
}

void CScriptedDataFunction::Load( class IScriptDataLoader& loader )
{
	loader >> (Uint8&)m_visibility;

	Uint32 flags = 0;
	loader >> flags;
	
	m_isStatic			= !!( flags & SDFF_IsStatic );
	m_isExec			= !!( flags & SDFF_IsExec );
	m_isTimer			= !!( flags & SDFF_IsTimer );
	m_isFinal			= !!( flags & SDFF_IsFinal );
	m_isImported		= !!( flags & SDFF_IsImported );
	m_isEvent			= !!( flags & SDFF_IsEvent );
	m_isOperator		= !!( flags & SDFF_IsOperator );
	m_isCast			= !!( flags & SDFF_IsCast );
	m_isCastImplicit	= !!( flags & SDFF_IsCastImplicit );
	m_isMulticast		= !!( flags & SDFF_IsMulticast );
	m_isHost			= !!( flags & SDFF_IsServer );
	m_isClient			= !!( flags & SDFF_IsClient );
	m_isReliable		= !!( flags & SDFF_IsReliable );
	m_isConst			= !!( flags & SDFF_IsConst );
	m_isThreadSafe		= !!( flags & SDFF_IsThreadSafe );
	m_isQuest			= !!( flags & SDFF_IsQuest );
	m_isTestOnly		= !!( flags & SDFF_IsTestOnly );

	// Load script defined function debug info
	if ( !m_isImported )
	{
		loader >> m_sourceFileInfo;
		loader >> m_sourceLine;
	}

	if ( flags & SDFF_HasRetType )
		m_returnValue.Load( loader );

	if ( flags & SDFF_HasSuperFunc )
		loader >> m_superFunc;

	if ( flags & SDFF_HasParams )
		loader >> m_params;

	if ( flags & SDFF_HasLocals )
		loader >> m_locals;

	if ( m_isOperator )
		loader >> m_operationName;

	if ( m_isCast )
		loader  >> m_castCost;

	if ( flags & SDFF_HasRawCode )
	{
		Uint32 size = 0;
		loader >> size;

		// setup array size
		m_rawCode.Resize( size );

		// transform code that we have on disk into a memory representation
		Helper::CodeDiskToMemory codeTransformer( m_rawCode.Data(), (Uint32)m_rawCode.DataSize(), loader );
		codeTransformer.PerformTransform();
	}
}

CScriptedDataFunction& CScriptedDataFunction::operator=( const CScriptedDataFunction& other )
{
	if ( this != &other )
	{
		m_visibility = other.m_visibility;
		m_isStatic = other.m_isStatic;
		m_isExec = other.m_isExec;
		m_isTimer = other.m_isTimer;
		m_isFinal = other.m_isFinal;		
		m_isImported = other.m_isImported;
		m_isImportedOverride = other.m_isImportedOverride;
		m_isEvent = other.m_isEvent;
		m_isOperator = other.m_isOperator;
		m_isCast = other.m_isCast;
		m_isCastImplicit = other.m_isCastImplicit;
		m_isMulticast = other.m_isMulticast;
		m_isHost = other.m_isHost;
		m_isClient = other.m_isClient;
		m_isReliable = other.m_isReliable;
		m_isConst = other.m_isConst;
		m_isThreadSafe = other.m_isThreadSafe;
		m_isQuest = other.m_isQuest;
		m_isTestOnly = other.m_isTestOnly;
		m_returnValue = other.m_returnValue;
		m_superFunc = other.m_superFunc;
		m_operationName = other.m_operationName;
		m_castCost = other.m_castCost;
		m_sourceFileInfo = other.m_sourceFileInfo;
		m_sourceLine = other.m_sourceLine;

		m_params.Clear();
		for ( const auto& param : other.m_params )
		{
			if ( CScriptedDataFunctionParam* const newParam = AddParam( param->GetName() ) )
			{
				*newParam = *param;
			}
		}

		m_locals.Clear();
		for ( const auto& local : other.m_locals )
		{
			if ( CScriptedDataFunctionLocal* const newLocal = AddLocal( local->GetName() ) )
			{
				*newLocal = *local;
			}
		}

		m_rawCode.Clear();
		for ( const auto& code : other.m_rawCode )
		{
			m_rawCode.PushBack( code );
		}
	}
	
	return *this;
}