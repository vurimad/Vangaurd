/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollectionClipboardHelpers.h"

#include "editableCollectionInterop.h"
#include "resolvedView.h"
#include "editableCollection.h"
#include "resourceEditorUserContext.h"

#include "../../../common/core/include/clipboardBase.h"

#include "../../../common/redReflection/include/serializableMap.h"

#include "../../../common/resource/include/jsonResource.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resource/include/jsonParser.h"
#include "../../../common/redReflection/include/editableView.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( PathAndValue, tools, prv )
	RTTI_PARENT_TYPE( ISerializable )
	RTTI_PROPERTY( m_path );
	RTTI_PROPERTY( m_value );
RTTI_END_TYPE()

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( BaseClipboardHolder, tools, prv )
	RTTI_PARENT_TYPE( ISerializable )
	RTTI_PROPERTY( m_exports );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( SingleElementClipboardHolder, tools, prv )
	RTTI_PARENT_TYPE( BaseClipboardHolder )
	RTTI_PROPERTY( m_value );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( CategoryClipboardHolder, tools, prv )
	RTTI_PARENT_TYPE( BaseClipboardHolder )
	RTTI_PROPERTY( m_pathsAndValues );
RTTI_END_TYPE()

namespace tools
{
	namespace prv
	{
		THandle< ISerializable > ExtractObject( const String& handleValue )
		{
			SerializableID id;
			FromString( handleValue, id );

			THandle< ISerializable > object;
			GSerializableMap->FindSerializable( id, object );

			return object;
		}

		PathAndValue::PathAndValue( const rtti::AccessPath& path )
			: m_path( path.ToString() )
		{
		}

		Uint32 BaseClipboardHolder::AddExport( const THandle< ISerializable >& object )
		{
			m_exports.PushBack( object );
			return m_exports.Size() - 1;
		}

		THandle< ISerializable > BaseClipboardHolder::GetExport( Uint32 index ) const
		{
			RED_FATAL_ASSERT( index < m_exports.Size() );
			return m_exports[index];
		}

		rtti::ValuePtr BaseClipboardHolder::ExtractExports( rtti::ValuePtr& value )
		{
			if ( value->IsHandle() )
			{
				Uint32 exportIndex = AddExport( ExtractObject( value->GetHandleValue() ) );
				value = rtti::ValueHolder::CreateHandle( ToStringDirect( exportIndex ).AsChar() );
			}
			else if ( value->IsArray() )
			{
				for ( Uint32 index = 0; index < value->GetNumElements(); ++index )
				{
					rtti::ValuePtr arrayElement = value->GetArrayElement( index );
					rtti::ValuePtr overriddenElement = ExtractExports( arrayElement );
					value->SetArrayElement( index, overriddenElement );
				}
			}
			else if ( value->IsStructure() )
			{
				for ( Uint32 structElemIndex = 0; structElemIndex < value->GetNumElements(); ++structElemIndex )
				{
					const CName structElemName = value->GetStructElementName( structElemIndex );
					rtti::ValuePtr structElement = value->GetStructElement( structElemName );
					rtti::ValuePtr overridenElement = ExtractExports( structElement );
					value->SetStructElement( structElemName, overridenElement );
				}
			}

			return value;
		}

		rtti::ValuePtr BaseClipboardHolder::SupplementExports( rtti::ValuePtr value )
		{
			if ( value->IsHandle() )
			{
				THandle< ISerializable > exportedObject = GetExport( FromStringDirect< Uint32 >( value->GetHandleValue() ) );
				if ( exportedObject )
				{
					// Copied object will have ClipboardHolder as parent, which in some cases (entity editor refreshing template view) breaks it serialization
					// StructureMapper::ShouldMapObject will not map it, because it will think it's outside of the scope. Setting the parent to nullptr fixes that issue
					// Related issue (CYB-113739)
					const rtti::ClassType* instanceClassType = exportedObject->GetClass();
					instanceClassType->RebuildParentHierarchy( exportedObject.Get(), nullptr );
					SerializableID trueHandleID = exportedObject->GetID();
					value = rtti::ValueHolder::CreateHandle( ToStringDirect( trueHandleID.Get() ).AsChar() );
				}
			}
			else if ( value->IsArray() )
			{
				for ( Uint32 index = 0; index < value->GetNumElements(); ++index )
				{
					rtti::ValuePtr arrayElement = value->GetArrayElement( index );
					rtti::ValuePtr overriddenElement = SupplementExports( arrayElement );
					value->SetArrayElement( index, overriddenElement );
				}
			}
			else if ( value->IsStructure() )
			{
				for ( Uint32 index = 0; index < value->GetNumElements(); ++index )
				{
					const CName structElemName = value->GetStructElementName( index );
					rtti::ValuePtr structElement = value->GetStructElement( structElemName );
					rtti::ValuePtr overridenElement = SupplementExports( structElement );
					value->SetStructElement( structElemName, overridenElement );
				}
			}

			return value;
		}

		Bool BaseClipboardHolder::CopyValue( const EditableCollectionRootItem* rootItem, const rtti::AccessPath& path, String& outValue )
		{
			rtti::ValuePtr value;
			if ( !rootItem->ReadValue( IRTTIContext::GetDefault(), path, value ) )
			{
				return false;
			}

			rtti::ValuePtr overriddenValue = ExtractExports( value );
			outValue = overriddenValue->ToString();

			return true;
		}

		Bool BaseClipboardHolder::PasteValue( EditableCollectionRootItem* rootItem, const rtti::AccessPath& path, const String& value )
		{
			if ( rootItem->GetResolvedView()->IsReadOnly( path ) )
				return false;

			rtti::ValuePtr pastedValue = rtti::ValueHolder::Parse( value.AsChar() );
			rtti::ValuePtr thisValue;
			if ( !rootItem->ReadValue( IRTTIContext::GetDefault(), path, thisValue ) || thisValue == nullptr )
			{
				return false;
			}

			if ( !( thisValue->IsEmpty() || pastedValue->IsEmpty() || thisValue->AreTypesSame( *pastedValue ) ) )
			{
				return false;
			}

			red::DynArray< rtti::ValuePtr > valuesToPaste;
			SerializableHandle userContext = rootItem->GetNegotiatedView()->GetUserContext();
			THandle< IResolverUserContext > resolver = Cast< IResolverUserContext >( userContext );
			for ( const EditableViewPtr& view : rootItem->GetResolvedView()->GetViews() )
			{
				rtti::ValuePtr valueToPaste = pastedValue;

				// try to resolve pasted object
				if ( resolver )
				{
					if ( !resolver->OnPrePaste( view->GetSerializableID(), path, *pastedValue.Get(), valueToPaste ) )
					{
						// pasted value rejected - hold your horses and cancel the whole operation
						return false;
					}
				}
				valuesToPaste.PushBack( valueToPaste );
			}

			for ( Uint32 i = 0; i < valuesToPaste.Size(); ++i )
			{
				rtti::ValuePtr overridenValue = SupplementExports( valuesToPaste[i] );
				valuesToPaste[i] = overridenValue;
			}
			return rootItem->WriteValue( IRTTIContext::GetDefault(), path, valuesToPaste );
		}

		SingleElementClipboardHolder::SingleElementClipboardHolder( const rtti::AccessPath& path )
			: m_path( path )
		{
		}

		SingleElementClipboardHolder::SingleElementClipboardHolder( const rtti::AccessPath& path, const String& value )
			: m_path( path )
			, m_value( value )
		{
		}

		Bool SingleElementClipboardHolder::Copy( interop::IContext& ctx, const EditableCollectionRootItem* rootItem )
		{
			return CopyValue( rootItem, m_path, m_value );
		}

		Bool SingleElementClipboardHolder::Paste( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths )
		{
			if ( paths.Size() != 1 )
			{
				ctx.ThrowException( "" );
				return false;
			}

			const rtti::AccessPath& path = paths[0];

			Bool result = PasteValue( rootItem, path, m_value );
			rootItem->RefreshView( false );
			return result;
		}

		CategoryClipboardHolder::CategoryClipboardHolder( const CName categoryName, const red::DynArray< rtti::AccessPath >& pathsToCopy )
			: m_categoryName( categoryName )
		{
			for ( const rtti::AccessPath& path : pathsToCopy )
				m_pathsAndValues.PushBack( PathAndValue( path ) );
		}

		const Bool CategoryClipboardHolder::GetEntry( const rtti::AccessPath& accessPath, String& outValue ) const
		{
			const String& path( accessPath.ToString() );
			for ( const PathAndValue& entry : m_pathsAndValues )
			{
				if ( entry.m_path.EqualsNC( path ) )
				{
					outValue = entry.m_value;
					return true;
				}
			}

			return false;
		}

		Bool CategoryClipboardHolder::Copy( interop::IContext& ctx, const EditableCollectionRootItem* rootItem )
		{
			Bool result = true;
			for ( PathAndValue& pair : m_pathsAndValues )
			{
				result &= CopyValue( rootItem, rtti::AccessPath( pair.m_path.AsChar() ), pair.m_value );
			}

			return result;
		}

		Bool CategoryClipboardHolder::Paste( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths )
		{
			Bool result = true;
			for ( const rtti::AccessPath& path : paths )
			{
				String value;

				if ( !GetEntry( path, value ) )
					continue;

				result &= PasteValue( rootItem, path, value );
			}

			rootItem->RefreshView( false );

			return result;
		}

		void CopyProperties( interop::IContext& ctx, const EditableCollectionRootItem* rootItem, const THandle< BaseClipboardHolder >& holder )
		{
			holder->Copy( ctx, rootItem );

			serialization::json::JsonSerializer serializer( holder );
			String result;
			const Bool serializeDefaultValue = false;
			const Bool generateExtendedTypeInfo = false;
			serializer.Serialize( result, serializeDefaultValue, generateExtendedTypeInfo,
								  []( serialization::json::JsonWriter& builder, const rtti::IType* type, void* data ) -> Bool
			{
				// fallback logic
				rtti::ValuePtr valuePtr;
				if ( !type->ReadValue( IRTTIContext::GetDefault(), data, rtti::AccessPath(), valuePtr ) )
				{
					RED_LOG_ERROR( "CopyPropertyItemFunctor: JsonSerializer: Couldn't read %hs value", type->GetName().AsChar() );
					return false;
				}

				builder.String( valuePtr->ToString().AsChar() );

				return true;
			}
			);

			Utf16String clipboardData( result.AsChar() );
			GClipboard->Copy( clipboardData );
		}

		void PasteProperties( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths )
		{
			if ( !( GClipboard && GClipboard->HasData() ) )
				return;

			Utf16String clipboardData;
			GClipboard->Paste( clipboardData );

			if ( clipboardData.Empty() )
			{
				ctx.ThrowException( "No compatible data in clipboard" );
				return;
			}

			String data( UNICODE_TO_ANSI( clipboardData.AsChar() ) );

			if ( THandle< ResourceEditorUserContext > userContext = Cast< ResourceEditorUserContext >( rootItem->GetResolvedView()->GetNegotiatedView()->GetUserContext() ) )
			{
				if ( auto converter = userContext->GetConvertProvider() )
				{
					converter->Convert( data );
				}
			}

			THandle< JsonResource > obj = CreateHandle< JsonResource >();
			res::json::ParseJsonResourceInline( data.AsChar(), obj,
												[]( const res::json::JSONValue& elm, const rtti::IType* propType, void* propertyData ) -> Bool
			{
				// fallback logic
				rtti::ValuePtr valuePtr = rtti::ValueHolder::Parse( elm.GetString() );
				return propType->WriteValue( IRTTIContext::GetDefault(), propertyData, rtti::AccessPath(), *valuePtr );
			}
			);

			THandle< BaseClipboardHolder > holder = obj->GetRoot< BaseClipboardHolder >();

			if ( !holder )
			{
				ctx.ThrowException( "Parsed data has no root object" );
				return;
			}

			EditableCollection* collection = rootItem->GetResolvedView()->GetEditableCollection();
			collection->BeginTransaction();
			if ( !holder->Paste( ctx, rootItem, paths ) )
			{
				ctx.ThrowException( "Paste failed" );
			}
			collection->EndTransaction();
		}

		CopyPasteBaseFunctor::CopyPasteBaseFunctor( const rtti::AccessPath& path )
			: m_accessPath( path )
		{
		}

		//////////////////////////////////////////////////////////////////////////

		CopyPropertyItemFunctor::CopyPropertyItemFunctor( const EditableCollectionRootItem* root, const rtti::AccessPath& accessPath )
			: CopyPasteBaseFunctor( accessPath )
			, m_root( root )
		{
			RED_ASSERT( m_root );
		}

		interop::InteropString CopyPropertyItemFunctor::Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const
		{
			THandle< SingleElementClipboardHolder > holder = CreateHandle< SingleElementClipboardHolder >( m_accessPath );
			CopyProperties( ctx, m_root, holder );
			return interop::InteropString::EMPTY();
		}

		PastePropertyItemFunctor::PastePropertyItemFunctor( EditableCollectionRootItem* root, const rtti::AccessPath& accessPath )
			: CopyPasteBaseFunctor( accessPath )
			, m_root( root )
		{
			RED_ASSERT( m_root );
		}

		interop::InteropString PastePropertyItemFunctor::Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const
		{
			PasteProperties( ctx, m_root, red::DynArray< rtti::AccessPath >{ m_accessPath } );
			return interop::InteropString::EMPTY();
		}
	}
}