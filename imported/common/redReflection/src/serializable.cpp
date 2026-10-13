/**
* Copyright (c) 2007-17 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializable.h"
#include "serializableDebug.h"
#include "objectUtils.hpp"
#include "serializableMap.h"
#include "variant.h"

#include "rttiAccessPath.h"
#include "rttiValueHolder.h"

#include "defaultSerializableEditorView.h"
#include "serializableValueNotifier.h"
#include "resource.h"
#include "rttiClassBuilder.h"
#include "../../redFileSystem/include/file.h"

RTTI_BEGIN_ABSTRACT_TYPE( ISerializable );
RTTI_END_TYPE();

ISerializable::ISerializable()
	: m_parent( nullptr )
	, m_objectId( SerializableID::GenerateRuntimeId() )
{
}


ISerializable::ISerializable( const ISerializable& other )
	: m_parent( other.m_parent )
	, m_objectId( SerializableID::GenerateRuntimeId() )
{
}

ISerializable& ISerializable::operator=( const ISerializable& other )
{
	// do not copy internal handle nor objectId
	m_parent = other.m_parent;
	return *this;
}

ISerializable::~ISerializable()
{
#ifdef RED_ENABLE_SERIALIZABLE_DEBUG
	serializableDebug::Unregister( this );
#endif

#if !defined( RED_CONFIGURATION_FINAL ) && !defined( RED_PLATFORM_CONSOLE )
	GSerializableMap->Unregister( m_objectId );
#endif	

	RED_FATAL_ASSERT( m_weakHandle.Expired(), "ISerializable cannot be manually deleted if bound to a THandle!" );
}

void ISerializable::OnPreSave( const PreSaveContext& context ) 
{}

void ISerializable::OnPostLoad( const PostLoadContext& context ) 
{}

tools::EditorObjectID ISerializable::GetEditorObjectID() const
{
	return tools::EditorObjectID();
}

void ISerializable::OnSerialize( IFile& file )
{
	if ( file.IsReader() )
	{
		GetClass()->SerializeDiff( this, file, this, nullptr, nullptr );
	}
	else
	{
		const void* defaultData = GetBaseObjectData();
		const rtti::ClassType* defaultDataClass = GetBaseObjectClass();
		GetClass()->SerializeDiff( this, file, this, defaultData, defaultDataClass );
	}
}

Bool ISerializable::OnSerializeToText( text::ITextWriter& writer ) const
{
	return GetClass()->SerializeToText(writer, this);
}

Bool ISerializable::OnSerializeFromText( text::ITextReader& reader )
{
	return GetClass()->SerializeFromText(reader, this);
}

Bool ISerializable::OnPropertyMissing( CName, const rtti::Variant& )
{
	return false;
}

Bool ISerializable::OnPropertyTypeMismatch( CName, const rtti::Property*, const rtti::Variant& )
{
	return false;
}

Bool ISerializable::OnPropertyRangeMismatch( CName propertyName, const Float min, const Float max, const Float readValue )
{
	return false;
}

Bool ISerializable::OnPropertyCanSave( IFile&, CName, const rtti::Property* ) const
{
	return true;
}

Bool ISerializable::OnPropertyPreChange( const rtti::AccessPath&, red::SharedPtr< const rtti::ValueHolder >& )
{
	CResource * parentResource = red::FindParent< CResource >( this );
	if( parentResource )
	{
		return parentResource->MarkModified();
	}

	return true;
}

const res::ResourcePath & ISerializable::GetPath() const
{	
	CResource * parentResource = red::FindParent< CResource >( this );
	if( parentResource )
	{
		return parentResource->GetPath();
	}

	return res::ResourcePath::EMPTY;
}

void ISerializable::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
{	
	NotifyPropertyChanged( path, oldValue, newValue );
}

void ISerializable::NotifyPropertyChanged( const rtti::AccessPath& propertyPath, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
{
#ifndef NO_EDITOR
	// notify all listeners
	tools::SerializableValueNotificationDispatcher::GetInstance().NotifyPropertyChange( GetID(), propertyPath, oldValue, newValue );
#endif
}

const void* ISerializable::GetBaseObjectData() const
{
	return GetClass()->GetDefaultObject();
}

const rtti::ClassType* ISerializable::GetBaseObjectClass() const
{
	return GetClass();
}

String ISerializable::GetFriendlyName() const
{
	return GetClass()->GetName().AsChar();
}

red::SharedPtr< tools::IEditableView > ISerializable::CreateView(const red::SharedPtr< tools::IEditableView >& parentView /*=nullptr*/, const Bool isReadOnly /*= false*/, const CName viewClass /*= CName::NONE()*/, THandle< ISerializable > userContext /*= nullptr*/) const
{
	if ( !userContext && parentView )
	{
		userContext = parentView->GetUserContext();
	}

	// the view class is in our case just a RTTI class name we want to sub-class the object view to
	const rtti::ClassType* viewClassPtr = GetClass();
	if ( viewClass )
		viewClassPtr = GetRttiSystem().FindClass( viewClass );

	// incompatible class
	if ( !viewClassPtr || !IsA( viewClassPtr ) )
		return red::SharedPtr< tools::IEditableView >();

	// create new view
	// view is ultra-cheep until we actually do something with it
	// the views can be cached and usually are, especially in the default editor backend
	// NOTE: we can make any view of an object read only using external flag
	
	THandle< ISerializable > handle = HandleFromThis();
	RED_FATAL_ASSERT( handle, "Cannot create View from a ISerializable not owned by a THandle" );
	red::SharedPtr< tools::IEditableView > ret = red::CreateSharedPtr< tools::DefaultSerializableEditorView >(parentView, handle, isReadOnly, viewClassPtr, userContext );
	return ret;
}

Bool ISerializable::Editor_OnInitialize()
{
	return false;
}

void ISerializable::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
{
	// no extra properties
}

Bool ISerializable::ReadCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, red::SharedPtr< rtti::ValueHolder >& outValue ) const
{
	return false;
}

Bool ISerializable::WriteCustomEditableProperty( IRTTIContext& ctx, const CName propertyName, const rtti::AccessPath& restOfThePath, const red::SharedPtr< rtti::ValueHolder >& newValue )
{
	return false;
}

bool ISerializable::OnDestructionRequest() 
{
	return true; // By default, we can delete ISerializable now!
}

void ISerializable::SetParent( const ISerializable* parent )
{
	RED_FATAL_ASSERT( this != parent, "Parent cannot be this pointer." );
	
	if( parent )
	{
		m_parent = parent->HandleFromThisInternal();		
	}
	else
	{
		m_parent.Reset();
	}

	RED_FATAL_ASSERT( !CheckForCircularDependency(), "Circular Dependency found! FIX YOUR CODE ASAP." );
}

bool ISerializable::CheckForCircularDependency()
{
	SerializableHandle parent = m_parent.ToHandle();

	while( parent )
	{
		if( parent.Get() == this )
		{
			return true;
		}

		parent = parent->m_parent.ToHandle();
	}

	return false;
}

ISerializable* ISerializable::GetParent() const 
{
	SerializableHandle parentHandle = m_parent.ToHandle();
	return parentHandle.Get(); 
}


THandle< ISerializable > ISerializable::HandleFromThis() const
{
	RED_FATAL_ASSERT( !m_weakHandle.Expired(), "Cannot create handle from non-shared object." );
	return THandle< ISerializable >( m_weakHandle );
}

WeakHandle< ISerializable > ISerializable::WeakHandleFromThis() const
{
	RED_FATAL_ASSERT( !m_weakHandle.Expired(), "Cannot create weak handle from non-shared object." );
	return m_weakHandle;
}

WeakHandle< ISerializable > ISerializable::Unsafe_WeakHandleFromThis() const
{
	// Do not ASSERT. We need this to convert raw pointer to smart pointer in some edge cases.
	return m_weakHandle;
}

THandle< ISerializable > ISerializable::HandleFromThisInternal() const
{
	return THandle< ISerializable >( m_weakHandle );
}

void ISerializable::InternalSetWeakHandle( const THandle< ISerializable > & handle )
{
	RED_FATAL_ASSERT( m_weakHandle.Expired(), "WeakHandle already assigned. 2 different THandle can't point to same ISerializable." );
	m_weakHandle = handle;
#if !defined( RED_CONFIGURATION_FINAL ) && !defined( RED_PLATFORM_CONSOLE )
	GSerializableMap->Register( m_objectId, m_weakHandle );
#endif	
}
