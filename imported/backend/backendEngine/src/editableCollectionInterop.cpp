/**
* Copyright (c) 2015-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollectionInterop.h"

#include "editableCollectionDataControlBackend.h"
#include "editableCollectionClipboardHelpers.h"
#include "resolvedView.h"

#include "../../../common/redReflection/include/editableView.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/generalEditorBackend/include/interopHierarchyWrappers.h"

namespace tools
{
	namespace prv
	{
		InteropRTTIContextAdapter::InteropRTTIContextAdapter( interop::IContext& ctx )
			: m_context( &ctx )
		{}

		InteropRTTIContextAdapter::~InteropRTTIContextAdapter() = default;

		const Bool InteropRTTIContextAdapter::ReportError( const rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... )
		{
			AnsiChar buf[ 1024 ];
			va_list args;

			va_start( args, txt );
			red::VSNPrintF( buf, RED_ARRAY_COUNT( buf ), txt, args );
			va_end( args );

			if ( typeInfo != nullptr )
				return m_context->ThrowException( "RTTI exception, type '%hs': %hs", typeInfo->GetName().AsChar(), buf );
			else
				return m_context->ThrowException( "RTTI exception: %hs", buf );
		}

		///
		///
		///

		interop::ObjectPtr CreatePropertyWrapper( EditableCollection* collection, EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::ClassEditablePropertyInfo& prop );

		CName GetEnumTypeName()
		{
			return RED_NAME( String::Printf( "%hs:Restricted", GetTypeName<CName>().AsChar() ).AsChar() );
		}

		CName GetBitFieldTypeName( Bool isMask )
		{
			if ( isMask )
			{
				return RED_NAME_CONSTEXPR( "Mask" );
			}
			return RED_NAME( String::Printf( "%hs:Restricted", GetTypeName<CName>().AsChar() ).AsChar() );
		}

		CName GetPropertyItemTypeName( const CName& typeName, const String& keyPath )
		{
			const String keyToken = keyPath.Empty() ? String::EMPTY() : String::Printf( ":key=%hs", keyPath.AsChar() );
			return RED_NAME( String::Printf( "%hs%hs", typeName.AsChar(), keyToken.AsChar() ).AsChar() );
		}

		CName GetArrayTypeName( const rtti::IBaseArrayType* arrayType, Bool resizable, const String& keyPath )
		{
			const String keyToken = keyPath.Empty() ? String::EMPTY() : String::Printf( ":key=%hs", keyPath.AsChar() );
 			return resizable ? RED_NAME( String::Printf( "dynarray:%hs%hs", arrayType->ArrayGetInnerType()->GetName().AsChar(), keyToken.AsChar() ).AsChar() ) :
 				RED_NAME( String::Printf( "%hs%hs", arrayType->GetName().AsChar(), keyToken.AsChar() ).AsChar() );
		}

		//--------

		EditableCollectionCustomPropertyItem::EditableCollectionCustomPropertyItem(red::SharedPtr<IEditableCustomProperty> control, const rtti::AccessPath& pathSoFar)
			: m_control( control )
			, m_path( pathSoFar )
		{
			m_control->GetRoot()->RegisterItem( this );
		}

		EditableCollectionCustomPropertyItem::~EditableCollectionCustomPropertyItem()
		{
			m_control->GetRoot()->UnregisterItem( this );
		}

		void EditableCollectionCustomPropertyItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr value;
			if ( !m_control->GetValue( adapter, m_path, value ) )
			{
				ctx.ThrowException( "Unable to read value from collection root" );
				return;
			}

			outValue = value->ToString();
		}

		void EditableCollectionCustomPropertyItem::Set(interop::IContext& ctx, const interop::InteropString& newValue) const  
		{
			auto newValuePtr = rtti::ValueHolder::Parse( newValue.AsChar() );
			if ( !newValuePtr )
			{
				ctx.ThrowException( "Unable to parse new value" );
				return;
			}

			InteropRTTIContextAdapter adapter( ctx );
			if( !m_control->SetValue( adapter, m_path, newValuePtr ) )
			{
				ctx.ThrowException( "Unable to write new value" );
				return;
			}
		}

		const Bool EditableCollectionCustomPropertyItem::IsReadOnly() const  
		{
			return m_control->IsReadOnly();
		}

		const Uint8 EditableCollectionCustomPropertyItem::IsDefault() const  
		{
			Uint8 isDefault;
			if ( m_control->IsDefault( IRTTIContext::GetDefault(), m_path, isDefault ) )
			{
				return isDefault;
			}

			return 0;
		}

		const Bool EditableCollectionCustomPropertyItem::IsMultiValue() const
		{
			return m_control->IsMultiValue();
		}

		const CName EditableCollectionCustomPropertyItem::GetType() const  
		{
			return m_control->GetControlType();
		}

		interop::ObjectPtr EditableCollectionCustomPropertyItem::GetUnlistedChild(interop::IContext& ctx, const String& name) const  
		{
			if ( name == "_options" && m_control->HasOptions() )
			{
				InteropRTTIContextAdapter adapter( ctx );
				m_control->RefreshOptions();
				const auto& options = m_control->GetOptions( adapter, m_path );

				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< String > > )( options ) );
			}
			else if ( name == "_reset" && m_control->CanReset() )
			{
				return interop::ObjectPtr( RED_NEW( ResetFunctor )( m_control->GetRoot(), m_path ) );
			}
			else if ( name == "_copy" )
			{
				return interop::ObjectPtr( RED_NEW( CopyPropertyItemFunctor )( m_control->GetRoot(), m_path ) );
			}
			else if ( name == "_paste" )
			{
				return interop::ObjectPtr( RED_NEW( PastePropertyItemFunctor )( m_control->GetRoot(), m_path ) ); 
			}
			else if ( name == "_tooltip" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_control->GetTooltip() ) );
			}
			else if( m_control->HasUnlistedChildren() )
			{
				return m_control->GetUnlistedChild( ctx, name );
			}
			else if ( name == "_id" )
			{
				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< proxy::GenericId > )( m_control->GetRoot()->GetItemId( this ) ) );
			}

			return ::interop::ObjectPtr();
		}

		const Bool EditableCollectionCustomPropertyItem::ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const
		{
			m_control->GetRoot()->ProcessNotification( this );
			return true;
		}

		void EditableCollectionCustomPropertyItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			String path = m_path.ToString();
			path.Replace( oldPath.ToString(), newPath.ToString() );
			m_path = rtti::AccessPath( path.AsChar() );

			m_control->Rebind( oldPath, newPath );
		}

		//----------------

		EditableCollectionCustomStructItem::EditableCollectionCustomStructItem( red::SharedPtr<IEditableCustomProperty> control, const EditableCollectionContext & context )
			: EditableCollectionStructItem( context )
			, m_control( control )
		{}

		interop::ObjectPtr EditableCollectionCustomStructItem::GetChild( interop::IContext& ctx, CName name ) const
		{
			if ( interop::ObjectPtr child = m_control->GetChild( ctx, name ) )
			{
				return child;
			}
			return EditableCollectionStructItem::GetChild( ctx, name );
		}

		void EditableCollectionCustomStructItem::GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const
		{
			if( !m_control->GetChildrenInfo(ctx, outInfo) )
			{
				EditableCollectionStructItem::GetChildrenInfo(ctx, outInfo);
			}
		}

		const Bool EditableCollectionCustomStructItem::IsMultiValue() const
		{
			return m_control->IsMultiValue();
		}

		const Bool EditableCollectionCustomStructItem::IsReadOnly() const
		{
			return m_control->IsReadOnly();
		}

		const CName EditableCollectionCustomStructItem::GetType() const
		{
			return m_control->GetControlType();
		}

		interop::ObjectPtr EditableCollectionCustomStructItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_options" && m_control->HasOptions() )
			{
				m_control->RefreshOptions();
				const auto& options = m_control->GetOptions();
				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< String > > )( options ) );
			}
			else if ( name == "_tooltip" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_control->GetTooltip() ) );
			}
			else if ( m_control->HasUnlistedChildren() )
			{
				if ( interop::ObjectPtr result = m_control->GetUnlistedChild( ctx, name ) )
 				{
 					return result;
 				}
			}

			return EditableCollectionStructItem::GetUnlistedChild( ctx, name );
		}

		void EditableCollectionCustomStructItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr value;
			if ( !m_control->GetValue( adapter, GetPath(), value ) )
			{
				ctx.ThrowException( "Unable to read value from collection root" );
				return;
			}

			outValue = value->ToString();
		}

		void EditableCollectionCustomStructItem::Set( interop::IContext& ctx, const interop::InteropString& newValue ) const
		{
			auto newValuePtr = rtti::ValueHolder::Parse( newValue.AsChar() );
			if ( !newValuePtr )
			{
				ctx.ThrowException( "Unable to parse new value" );
				return;
			}

			InteropRTTIContextAdapter adapter( ctx );
			if ( !m_control->SetValue( adapter, GetPath(), newValuePtr ) )
			{
				ctx.ThrowException( "Unable to write new value" );
				return;
			}
		}

		void EditableCollectionCustomStructItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			EditableCollectionStructItem::Rebind( oldPath, newPath );
			m_control->Rebind( oldPath, newPath );
		}

		//----------------

		EditableCollectionPropertyItem::EditableCollectionPropertyItem( const EditableCollectionContext & context )
			: m_collection( context.collection )
			, m_root( context.root )
			, m_hasMultipleValues( false )
			, m_isReadOnly( context.propertyInfo.m_isReadOnly )
			, m_type( context.propertyInfo.m_type )
			, m_path( context.path )
			, m_canReset( context.propertyInfo.m_canReset )
			, m_tooltip( context.propertyInfo.m_tooltip )
		{
			if ( context.propertyInfo.m_customTypeName )
			{
				m_typeName = context.propertyInfo.m_customTypeName;
			}
			else
			{
				m_typeName = m_type->GetName();
			}

			m_root->RegisterItem( this );
		}

		EditableCollectionPropertyItem::~EditableCollectionPropertyItem()
		{
			m_root->UnregisterItem( this );
		}

		void EditableCollectionPropertyItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr value;
			if ( !m_root->ReadValue( adapter, m_path, value ) )
			{
				ctx.ThrowException( "Unable to read value from collection root" );
				return;
			}

			FormatReturnValue( value, outValue );
		}

		void EditableCollectionPropertyItem::Set( interop::IContext& ctx, const interop::InteropString& newValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr newValuePtr = FormatInputValue( newValue );
			if ( !newValuePtr )
			{
				ctx.ThrowException( "Unable to parse new value" );
				return;
			}

			if ( !m_root->WriteValue( adapter, m_path, newValuePtr ) )
			{
				ctx.ThrowException( "Unable to write new value" );
				return;
			}
		}

		interop::ObjectPtr EditableCollectionPropertyItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_copy" )
			{
				return interop::ObjectPtr( RED_NEW( CopyPropertyItemFunctor )( m_root, m_path ) );
			}
			else if ( name == "_paste" )
			{
				return interop::ObjectPtr( RED_NEW( PastePropertyItemFunctor )( m_root, m_path ) ); 
			}
			else if ( m_canReset && name == "_reset" )
			{
				return interop::ObjectPtr( RED_NEW( ResetFunctor )( m_root, m_path ) );
			}
			else if ( name == "_tooltip" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_tooltip ) );
			}
			else if ( name == "_id" )
			{
				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< proxy::GenericId > )( m_root->GetItemId( this ) ) );
			}

			return interop::ObjectPtr();
		}

		void EditableCollectionPropertyItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			String path = m_path.ToString();
			path.Replace( oldPath.ToString(), newPath.ToString() );
			m_path = rtti::AccessPath( path.AsChar() );
		}

		const Bool EditableCollectionPropertyItem::IsReadOnly() const
		{
			return m_isReadOnly;
		}

		const Uint8 EditableCollectionPropertyItem::IsDefault() const
		{
			Uint8 isDefault;
			if ( m_root->IsDefault( IRTTIContext::GetDefault(), m_path, isDefault ) )
			{
				return isDefault;
			}

			return 0;
		}

		const Bool EditableCollectionPropertyItem::IsMultiValue() const
		{
			return m_root->IsMultiValue( m_path );
		}

		const CName EditableCollectionPropertyItem::GetType() const
		{
			return m_typeName;
		}

		const Bool EditableCollectionPropertyItem::ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const
		{
			rtti::PathParser parser( path );
			if ( !parser )
			{
				m_root->ProcessNotification( this );
				return true;
			}

			// not notified
			return false;
		}

		void EditableCollectionPropertyItem::FormatReturnValue( const rtti::ValuePtr& value, interop::InteropString& outValue ) const
		{
			outValue = value->ToString();
		}

		rtti::ValuePtr EditableCollectionPropertyItem::FormatInputValue( const interop::InteropString& value ) const
		{
			return rtti::ValueHolder::Parse( value.AsChar() );
		}

		//----------------

		EditableCollectionEnumPropertyItem::EditableCollectionEnumPropertyItem( const EditableCollectionContext & context )
			: EditableCollectionPropertyItem( context )
		{
			m_enumType = GetRttiSystem().FindEnum( context.propertyInfo.m_type->GetName() );
		}

		interop::ObjectPtr EditableCollectionEnumPropertyItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_options" )
			{				
				const red::DynArray<CName>& options = m_enumType->GetOptions();

				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< CName > > )( options ) );
			}

			return EditableCollectionPropertyItem::GetUnlistedChild( ctx, name );
		}

		//------------------------------

		EditableCollectionBitfieldPropertyItem::EditableCollectionBitfieldPropertyItem( const EditableCollectionContext & context )
			: EditableCollectionPropertyItem( context )
		{
			m_bitfieldType = GetRttiSystem().FindBitField( context.propertyInfo.m_type->GetName() );
		}

		interop::ObjectPtr EditableCollectionBitfieldPropertyItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_options" )
			{
				red::DynArray<CName> options{ red::PoolBackend() };
				m_bitfieldType->GetOptions( options );

				return ::interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< red::DynArray< CName > > )( options ) );
			}

			return EditableCollectionPropertyItem::GetUnlistedChild( ctx, name );
		}

		void EditableCollectionBitfieldPropertyItem::FormatReturnValue( const rtti::ValuePtr& value, interop::InteropString& outValue ) const
		{
			outValue = value->GetSingleValue(); 
			outValue.TrimLeft( '<' ).TrimRight( '>' );
		}

		rtti::ValuePtr EditableCollectionBitfieldPropertyItem::FormatInputValue( const interop::InteropString& value ) const
		{
			const String formattedValue = String::Printf( "\"<%hs>\"", rtti::ValueHolder::Parse( value.AsChar() )->GetSingleValue().AsChar() );
			return rtti::ValueHolder::Parse( formattedValue.AsChar() );
		}

		//------------------------------

		static CName GetRangedTypeName( const CName typeName )
		{
			return RED_NAME( String::Printf( "%hs:Ranged", typeName.AsChar() ).AsChar() );
		}

		EditableCollectionRangedPropertyItem::EditableCollectionRangedPropertyItem( const EditableCollectionContext & context )
			: EditableCollectionPropertyItem( context )
			, m_minValue( context.propertyInfo.m_minValue )
			, m_maxValue( context.propertyInfo.m_maxValue )
			, m_xMinValue( context.propertyInfo.m_xMinValue )
			, m_xMaxValue( context.propertyInfo.m_xMaxValue )
			, m_useSlider( !context.propertyInfo.m_hideSlider )
		{
		}

		interop::ObjectPtr EditableCollectionRangedPropertyItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( m_minValue < m_maxValue )
			{
				if (name == "_min")
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Float> )( m_minValue ) );
				}
				else if (name == "_max")
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Float> )( m_maxValue ) );
				}
			}

			if (m_xMinValue < m_xMaxValue)
			{
				if (name == "_xMin")
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< Float > )( m_xMinValue ) );
				}

				if (name == "_xMax")
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< Float > )( m_xMaxValue ) );
				}
			}

			if ( name == "_useSlider" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_useSlider) );
			}

			return EditableCollectionPropertyItem::GetUnlistedChild( ctx, name );
		}

		//------------------------------

		class ArrayClearFunctor : public interop::IFunctorObject
		{
		public:
			ArrayClearFunctor( const EditableCollectionArrayItem* parent )
				: m_parent( const_cast< EditableCollectionArrayItem* >( parent ) )
			{}

			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const
			{
				// no reported params
			}

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const
			{
				m_parent->ArrayClear( ctx );
				return interop::InteropString("");
			}

			virtual const CName GetType() const override final
			{
				return CName::NONE(); // void return type
			}

		private:
			EditableCollectionArrayItem*	m_parent;
		};

		//----------------

		class ArrayAddElementFunctor : public interop::IFunctorObject
		{
		public:
			ArrayAddElementFunctor( const EditableCollectionArrayItem* parent )
				: m_parent( const_cast< EditableCollectionArrayItem* >( parent ) )
			{}

			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const override final
			{
				// no reported params
			}

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const override final
			{
				m_parent->ArrayAddElement( ctx );
				return interop::InteropString("");
			}

			virtual const CName GetType() const override final
			{
				return CName::NONE(); // void return type
			}

		private:
			EditableCollectionArrayItem* m_parent;
		};

		//----------------

		class ArrayInsertElementFunctor : public interop::IFunctorObject
		{
		public:
			ArrayInsertElementFunctor( const EditableCollectionArrayItem* parent )
				: m_parent( const_cast< EditableCollectionArrayItem* >( parent ) )
			{}

			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const override final
			{
				outTypes.PushBack( ::GetTypeName<Uint32>() );
				// no reported params
			}

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const override final
			{
				if ( numArgs != 1 )
				{
					ctx.ThrowException( "insert() operations expects exactly one argument" );
					return interop::InteropString("");
				}

				const Uint32 index = ::FromStringDirect<Uint32>( args[0].AsChar() );

				m_parent->ArrayInsertElement( ctx, index );
				return interop::InteropString("");
			}

			virtual const CName GetType() const override final
			{
				return CName::NONE(); // void return type
			}

		private:
			EditableCollectionArrayItem*	m_parent;
		};

		//----------------

		class ArrayMoveElementFunctor : public interop::IFunctorObject
		{
		public:
			ArrayMoveElementFunctor( const EditableCollectionArrayItem* parent )
				: m_parent( const_cast< EditableCollectionArrayItem* >( parent ) )
			{}

			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const override final
			{
				outTypes.PushBack( ::GetTypeName< Uint32 >() );
				outTypes.PushBack( ::GetTypeName< Uint32 >() );
			}

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const override final
			{
				if ( numArgs != 2 )
				{
					ctx.ThrowException( "insert() operation expects exactly two arguments" );
					return interop::InteropString();
				}

				const Uint32 fromIndex = ::FromStringDirect< Uint32 >( args[0].AsChar() );
				const Uint32 toIndex = ::FromStringDirect< Uint32 >( args[1].AsChar() );

				m_parent->ArrayMoveElement( ctx, fromIndex, toIndex );
				return interop::InteropString();
			}

			virtual const CName GetType() const override final
			{
				return CName::NONE();
			}

		private:
			EditableCollectionArrayItem* m_parent;
		};

		//----------------

		EditableCollectionArrayItem::EditableCollectionArrayItem( const EditableCollectionContext & context )
			: m_collection( context.collection )
			, m_root( context.root )
			, m_path( context.path )
			, m_arrayType( static_cast< const rtti::IBaseArrayType * >( context.propertyInfo.m_type ) )
			, m_isReadOnly( context.propertyInfo.m_isReadOnly )
			, m_isInlined( context.propertyInfo.m_isInlined )
			, m_isResizable( context.propertyInfo.m_isResizable )
			, m_canAdd( context.propertyInfo.m_canAdd )
			, m_canClear( context.propertyInfo.m_canClear )
			, m_canSelect( context.propertyInfo.m_canSelect )
			, m_canReset( context.propertyInfo.m_canReset )
			, m_typeName( GetArrayTypeName( static_cast< const rtti::IBaseArrayType * >( context.propertyInfo.m_type ), context.propertyInfo.m_isResizable, context.propertyInfo.m_keyPath ) )
			, m_minSize( context.propertyInfo.m_minSize )
			, m_maxSize( context.propertyInfo.m_maxSize )
			, m_minValue( context.propertyInfo.m_minValue )
			, m_maxValue( context.propertyInfo.m_maxValue )
			, m_innerTypeCustomTypeName( context.propertyInfo.m_customTypeMode == rtti::CustomEditorMode::InnerType ? context.propertyInfo.m_customTypeName : CName::NONE() )
			, m_tooltip( context.propertyInfo.m_tooltip )
			, m_keyPath( context.propertyInfo.m_keyPath )
			, m_elements{ red::PoolBackend() }
		{
			m_root->RegisterItem( this );
			RebuildAll();
		}

		EditableCollectionArrayItem::~EditableCollectionArrayItem()
		{
			m_root->UnregisterItem( this );
		}

		void EditableCollectionArrayItem::RebuildAll()
		{
			// clear current elements
			m_elements.Clear();

			// capture elements
			rtti::ValuePtr arrayElements;
			if ( m_root->ReadValue( IRTTIContext::GetDefault(), m_path, arrayElements ) ) // NOTE: if the value is not determined it will return empty array or false
			{
				// create the elements
				if ( arrayElements->IsArray() )
				{
					const Uint32 count = arrayElements->GetNumElements();
					for ( Uint32 i = 0; i < count; ++i )
					{
						auto elementData = arrayElements->GetArrayElement( i );
						RED_ASSERT( elementData != nullptr, "Not supported case when data could not be retrieved from array" );
						if ( elementData )
						{
							rtti::ClassEditablePropertyInfo info;
							info.m_name = RED_NAME( m_path[0].ToString().AsChar() );
							info.m_type = m_arrayType->ArrayGetInnerType();
							info.m_isInlined = m_isInlined;
							info.m_isReadOnly = m_isReadOnly;
							info.m_customTypeName = m_innerTypeCustomTypeName;
							info.m_canClear = m_canClear;
							info.m_canSelect = m_canSelect;
							info.m_canReset = m_canReset;
							info.m_minValue = m_minValue;
							info.m_maxValue = m_maxValue;

							auto wrapper = CreatePropertyWrapper( m_collection, m_root, m_path[i], info );
							RED_ASSERT( wrapper != nullptr, "Not supported case when property wrapper could not be created for array element" );
							if ( wrapper )
							{
								m_elements.PushBack( wrapper );
							}
						}
					}
				}
			}

			m_root->ProcessNotification( this );
		}

		void EditableCollectionArrayItem::HandleDirectChange( const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			if ( !targetValue || !oldValue )
			{
				RebuildAll();
			}
			else if ( targetValue->IsArray() && oldValue->IsArray() )
			{
				const Int32 targetSize = targetValue->GetNumElements();
				const Int32 oldSize = oldValue->GetNumElements();
				const Int32 commonSize = math::Min( targetSize, oldSize );
				const Int32 sizeDiff = math::Abs<Int32>( oldSize - targetSize );

				if ( sizeDiff > 1 )
				{
					RebuildAll();
					return;
				}

				Int32 index;
				for ( index = 0; index < commonSize; ++index )
				{
					const rtti::ValuePtr currentElement = targetValue->GetArrayElement( index );
					const rtti::ValuePtr oldElement = oldValue->GetArrayElement( index );

					if ( currentElement->ToString() != oldElement->ToString() )
					{
						break;
					}
				}	

				if ( targetSize > oldSize && m_elements.Size() == oldSize ) // element added
				{
					CreateItem( index );
				}
				else if ( targetSize < oldSize && m_elements.Size() == oldSize ) // element removed
				{
					RemoveItem( index );
				}
				else if( commonSize > 0 && index < commonSize ) // targetSize == oldSize -> element was moved, unless both arrays are empty
				{
					Int32 secondDifferenceIndex;
					for ( secondDifferenceIndex = commonSize - 1; secondDifferenceIndex > index; --secondDifferenceIndex )
					{
						const rtti::ValuePtr currentElement = targetValue->GetArrayElement( secondDifferenceIndex );
						const rtti::ValuePtr oldElement = oldValue->GetArrayElement( secondDifferenceIndex );

						if ( currentElement->ToString() != oldElement->ToString() )
						{
							break;
						}
					}

					// this looks complicated but it's fairly simple, in a situation when there are multiple elements that serialize to the same string, we may have some trouble to distinguish those cases
					// e.g.	index	old value	target value
					//		0		1		/--	2				we move element 2 from the bottom to the top
					//		1		1	--/		1				the first element is the same as the last and vice versa
					//		2		1		/--	1				we check old-1 against target-0 or old-3 against target-2
					//		3		2	--/		1
					// second case is similar

					if ( ( targetValue->GetArrayElement( index )->ToString() == oldValue->GetArrayElement( secondDifferenceIndex )->ToString() )
						&& ( targetValue->GetArrayElement( secondDifferenceIndex - 1 )->ToString() != oldValue->GetArrayElement( secondDifferenceIndex )->ToString()
							|| targetValue->GetArrayElement( index )->ToString() != oldValue->GetArrayElement( index + 1 )->ToString() ) )
					{
						// element moved up
						interop::ObjectPtr object = m_elements[secondDifferenceIndex];
						m_elements.RemoveAt( secondDifferenceIndex );
						m_elements.InsertAt( index, object );

						RebindRange( index, index + 1, secondDifferenceIndex - index );
						RebindRange( index + 1, secondDifferenceIndex, -1 );
						m_root->NotifyItemMoved( object.Get(), index );
					}
					else if ( targetValue->GetArrayElement( secondDifferenceIndex )->ToString() == oldValue->GetArrayElement( index )->ToString() )
					{
						// element moved down
						interop::ObjectPtr object = m_elements[index];
						m_elements.RemoveAt( index );
						m_elements.InsertAt( secondDifferenceIndex, object );

						RebindRange( index, secondDifferenceIndex, 1 );
						RebindRange( secondDifferenceIndex, secondDifferenceIndex + 1, index - secondDifferenceIndex );
						m_root->NotifyItemMoved( object.Get(), secondDifferenceIndex );
					}
					else
					{
						RebuildAll();
					}
				}
			}
		}

		void EditableCollectionArrayItem::CreateItem( const Uint32 index )
		{
			rtti::ValuePtr element;
			if ( m_root->ReadValue( IRTTIContext::GetDefault(), m_path[index], element ) ) // NOTE: if the value is not determined it will return empty array or false
			{
				rtti::ClassEditablePropertyInfo info;
				info.m_name = RED_NAME( m_path[0].ToString().AsChar() );
				info.m_type = m_arrayType->ArrayGetInnerType();
				info.m_isInlined = m_isInlined;
				info.m_isReadOnly = m_isReadOnly;
				info.m_customTypeName = m_innerTypeCustomTypeName;
				info.m_canClear = m_canClear;
				info.m_canSelect = m_canSelect;
				info.m_canReset = m_canReset;

				auto wrapper = CreatePropertyWrapper( m_collection, m_root, m_path[index], info );
				RED_ASSERT( wrapper != nullptr, "Not supported case when property wrapper could not be created for array element" );
				if ( wrapper )
				{
					m_elements.InsertAt( index, wrapper );
					m_root->NotifyItemCreated( this, wrapper.Get(), index );
				}
			}

			RebindRange( index + 1, m_elements.Size(), -1 );
			m_root->ProcessNotification( this );
		}

		void EditableCollectionArrayItem::RemoveItem( const Uint32 index )
		{
			m_elements.RemoveAt( index );
			RebindRange( index, m_elements.Size(), 1 );
			m_root->ProcessNotification( this );
		}

		void EditableCollectionArrayItem::RebindRange( const Uint32 from, const Uint32 to, const Int32 indexOffset )
		{
			for ( Uint32 index = from; index < to; ++index )
			{
				const Uint32 oldIndex = index + indexOffset;
				interop::IBaseObject* baseItem = m_elements[index].Get();
				baseItem->Rebind( m_path[oldIndex], m_path[index] );
			}
		}

		const Uint32 EditableCollectionArrayItem::GetSize( interop::IContext& ctx ) const
		{
			return m_elements.Size();
		}

		void EditableCollectionArrayItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			String path = m_path.ToString();
			path.Replace( oldPath.ToString(), newPath.ToString() );
			m_path = rtti::AccessPath( path.AsChar() );

			for ( const interop::ObjectPtr& element : m_elements )
			{
				element->Rebind( oldPath, newPath );
			}
		}

		const interop::ObjectPtr EditableCollectionArrayItem::GetElement( interop::IContext& ctx, const Uint32 index ) const
		{
			if ( index >= m_elements.Size() )
			{
				ctx.ThrowException( "Out of bounds array access (%d > %d)", index, m_elements.Size() );
				return interop::ObjectPtr();
			}

			return m_elements[index];
		}

		const CName EditableCollectionArrayItem::GetType() const
		{
			return m_typeName;
		}

		const Bool EditableCollectionArrayItem::IsReadOnly() const
		{
			return m_isReadOnly;
		}

		const Bool EditableCollectionArrayItem::IsMultiValue() const
		{
			return m_root->IsMultiValue( m_path );
		}

		const Bool EditableCollectionArrayItem::IsResizable() const
		{
			return m_isResizable;
		}

		const Uint32 EditableCollectionArrayItem::GetMinSize() const
		{
			return m_minSize;
		}

		const Uint32 EditableCollectionArrayItem::GetMaxSize() const
		{
			return m_maxSize;
		}

		void EditableCollectionArrayItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );

			rtti::ValuePtr value;
			if ( !m_root->ReadValue( adapter, m_path, value ) )
			{
				ctx.ThrowException( "Unable to read value from array root" );
				return;
			}

			outValue = value->ToString();
		}

		void EditableCollectionArrayItem::Set( interop::IContext& ctx, const interop::InteropString& newValue )
		{
			rtti::ValuePtr newValues = rtti::ValueHolder::Parse( newValue.AsChar() );

			if( !newValues->IsArray() )
			{
				ctx.ThrowException( "Non-array data passed to array setter" );
				return;
			}

			red::DynArray< rtti::ValuePtr > oldValues { red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( oldValues, m_path );

			InteropRTTIContextAdapter adapter( ctx );

			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, oldValues, newValues ) )
			{
				ctx.ThrowException( "Array set operation failed" );
				return;
			}

			RebuildAll();
		}

		const Bool EditableCollectionArrayItem::ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const
		{
			// array changed directly (resize, element added, removed, etc)
			// do no trigger the onChange event if the array element is changed
			rtti::PathParser parser( path );
			Int32 index = -1;
			if ( !parser )
			{
				const_cast<EditableCollectionArrayItem*>( this )->HandleDirectChange( oldValue, targetValue );
				return true;
			}
			else if ( parser.EatIndex( index ) && index < static_cast< Int32 >( m_elements.Size() ) )
			{
				return m_elements[index]->ProcessNotification( parser.GetUneatenPath(), oldValue, targetValue );
			}

			return false;
		}

		interop::ObjectPtr EditableCollectionArrayItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_elementsMap" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::TypedMapObject< proxy::GenericId >)( [this]( interop::IContext& ctx, const proxy::GenericId elementId )
				{
					interop::IBaseObject* item = m_root->GetItemById( elementId );
					// could be changed to assert later on
					auto it = std::find_if( m_elements.Begin(), m_elements.End(), [item]( const interop::ObjectPtr& element ) { return element.Get() == item; } );

					return it != m_elements.End() ? *it : interop::ObjectPtr();
				} ) );
			}
			else if ( name == "_tooltip" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_tooltip ) );
			}
			else if ( m_canClear && name == "clear" )
			{
				return interop::ObjectPtr( RED_NEW( ArrayClearFunctor )( this ) );
			}
			else if ( m_isResizable && m_canAdd && name == "add" )
			{
				return interop::ObjectPtr( RED_NEW( ArrayAddElementFunctor )( this ) );
			}
			else if ( m_isResizable && m_canAdd && name == "insert" )
			{
				return interop::ObjectPtr( RED_NEW( ArrayInsertElementFunctor )( this ) );
			}
			else if ( m_isResizable && name == "remove" )
			{
				using FunctorType = interop::utils::Functor1< Bool, proxy::GenericId >;
				return interop::ObjectPtr( RED_NEW( FunctorType )( [this]( interop::IContext& ctx, const proxy::GenericId id )
				{
					interop::IBaseObject* item = m_root->GetItemById( id );
					auto it = std::find_if( m_elements.Begin(), m_elements.End(), [item]( const interop::ObjectPtr& element ) { return element.Get() == item; } );
					if ( it != m_elements.End() )
					{
						const Uint32 index = (Uint32)std::distance( m_elements.Begin(), it );
						return const_cast<EditableCollectionArrayItem*>( this )->ArrayRemoveElement( ctx, index );
					}

					return ctx.ThrowException( "Couldn't find item with given id in the array" );
				} ) );
			}
			else if ( m_isResizable && name == "removeIndex" )
			{
				using FunctorType = interop::utils::Functor1< Bool, Uint32 >;
				return interop::ObjectPtr( RED_NEW( FunctorType )( [this]( interop::IContext& ctx, const Uint32 index )
				{
					return const_cast<EditableCollectionArrayItem*>( this )->ArrayRemoveElement( ctx, index );
				} ) );
			}
			else if ( name == "move" )
			{
				return interop::ObjectPtr( RED_NEW( ArrayMoveElementFunctor )( this ) );
			}
			else if ( name == "swap" )
			{
				// TODO
			}
			else if ( name == "_keyPath" && !m_keyPath.Empty() )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_keyPath ) );
			}
			else if ( name == "_isResizable" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_isResizable ) );
			}
			else if ( name == "_canAdd" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_canAdd ) );
			}
			else if ( name == "_minSize" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Uint32> )( m_minSize ) );
			}
			else if ( name == "_maxSize" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Uint32> )( m_maxSize ) );
			}
			else if ( name == "_copy" )
			{
				return interop::ObjectPtr( RED_NEW( CopyPropertyItemFunctor )( m_root, m_path ) );
			}
			else if ( name == "_paste" )
			{
				return interop::ObjectPtr( RED_NEW( PastePropertyItemFunctor )( m_root, m_path ) ); 
			}
			else if ( m_canReset && name == "_reset" )
			{
				return interop::ObjectPtr( RED_NEW( ResetFunctor )( m_root, m_path ) );
			}
			else if ( name == "_id" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< proxy::GenericId > )( m_root->GetItemId( this ) ) );
			}

			return interop::ObjectPtr();
		}

		const Bool EditableCollectionArrayItem::ArrayClear( interop::IContext& ctx )
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr newValue = rtti::ValueHolder::CreateArray();
			if ( !m_root->WriteValue( adapter, m_path, newValue ) )
				return ctx.ThrowException( "Array operation failed" );

			RebuildAll();
			return true;
		}

		Bool IsAnyElementHandle( const rtti::ValuePtr& arrayValue )
		{
			if ( !arrayValue->IsArray() )
				return false;

			const Uint32 size = arrayValue->GetNumElements();
			for ( Uint32 index = 0; index < size; ++index )
			{
				if ( arrayValue->GetArrayElement( index )->IsEmpty() )
					continue;

				if ( arrayValue->GetArrayElement( index )->IsHandle() )
				{
					return true;
				}
				else
				{
					return false;
				}
			}

			return false;
		}

		const Bool EditableCollectionArrayItem::ArrayInsertElement( interop::IContext& ctx, const Uint32 index )
		{
			InteropRTTIContextAdapter adapter( ctx );

			red::DynArray< rtti::ValuePtr > arrayValues{ red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( arrayValues, m_path );

			if ( arrayValues.Empty() )
				return ctx.ThrowException( "Missing array property" );

			red::DynArray< rtti::ValuePtr > newArrayValues{ red::PoolBackend() };
			for ( rtti::ValuePtr& arrayValue : arrayValues )
			{
				// not array
				if ( !arrayValue->IsArray() )
					return ctx.ThrowException( "Array operations can be performed only on arrays" );

				// add element to array
				rtti::ValuePtr newArrayValue = rtti::ValueHolder::Parse( arrayValue->ToString().AsChar() );
				newArrayValue->InsertArrayElement( index, rtti::ValueHolder::CreateEmpty() );
				newArrayValues.PushBack( newArrayValue );
			}

			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, arrayValues, newArrayValues ) )
			{
				return ctx.ThrowException( "Array operation failed" );
			}

			// this is a hack to setup new handle values in arrays, there needs to be a cleaner solution
			if ( newArrayValues.Size() == 1 && IsAnyElementHandle( newArrayValues[0] ) )
			{
				m_root->ResetValue( IRTTIContext::GetDefault(), m_path[index] );
			}

			return true;
		}

		const Bool EditableCollectionArrayItem::ArrayAddElement( interop::IContext& ctx )
		{
			InteropRTTIContextAdapter adapter( ctx );

			red::DynArray< rtti::ValuePtr > arrayValues{ red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( arrayValues, m_path );

			if ( arrayValues.Empty() )
				return ctx.ThrowException( "Missing array property" );

			red::DynArray< rtti::ValuePtr > newArrayValues{ red::PoolBackend() };
			for ( rtti::ValuePtr& arrayValue : arrayValues )
			{
				// not array
				if ( !arrayValue->IsArray() )
					return ctx.ThrowException( "Array operations can be performed only on arrays" );

				// add element to array
				rtti::ValuePtr newArrayValue = rtti::ValueHolder::Parse( arrayValue->ToString().AsChar() );
				if( !m_maxSize || arrayValue->GetNumElements() < m_maxSize )
				{
					newArrayValue->AddArrayElement( rtti::ValueHolder::CreateEmpty() );
				}
				newArrayValues.PushBack( newArrayValue );
			}

			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, arrayValues, newArrayValues ) )
			{
				return ctx.ThrowException( "Array operation failed" );
			}

			// this is a hack to setup new handle values in arrays, there needs to be a cleaner solution
			if ( newArrayValues.Size() == 1 && ( newArrayValues[0]->GetArrayElement( 0 )->IsHandle() || newArrayValues[ 0 ]->GetArrayElement( 0 )->IsEmpty() ) )
			{
				m_root->ResetValue( IRTTIContext::GetDefault(), m_path[newArrayValues[0]->GetNumElements() - 1] );
			}

			return true;
		}

		const Bool EditableCollectionArrayItem::ArrayRemoveElement( interop::IContext& ctx, const Uint32 index )
		{
			InteropRTTIContextAdapter adapter( ctx );

			red::DynArray< rtti::ValuePtr > arrayValues{ red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( arrayValues, m_path );

			if ( arrayValues.Empty() )
				return ctx.ThrowException( "Missing array property" );

			red::DynArray< rtti::ValuePtr > newArrayValues{ red::PoolBackend() };
			for ( rtti::ValuePtr& arrayValue : arrayValues )
			{
				// not array
				if ( !arrayValue->IsArray() )
					return ctx.ThrowException( "Array operations can be performed only on arrays" );

				// out of range index ?
				if ( index >= arrayValue->GetNumElements() )
					return ctx.ThrowException( "Element index is out of range (%d >= %d)", index, arrayValue->GetNumElements() );

				// remove element from array
				rtti::ValuePtr newArrayValue = rtti::ValueHolder::Parse( arrayValue->ToString().AsChar() );
				newArrayValue->RemoveArrayElement( index );
				newArrayValues.PushBack( newArrayValue );
			}

			// write it back
			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, arrayValues, newArrayValues ) )
			{
				return ctx.ThrowException( "Array operation failed" );
			}

			return true;
		}

		const Bool EditableCollectionArrayItem::ArrayMoveElement( interop::IContext& ctx, const Uint32 from, const Uint32 to )
		{
			// the first one is obvious, the second behaves like the first, because we insert the element and then remove it, so the array stays the same
			if ( from == to || ( from + 1 ) == to )
				return true;

			InteropRTTIContextAdapter adapter( ctx );

			red::DynArray< rtti::ValuePtr > arrayValues{ red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( arrayValues, m_path );

			if ( arrayValues.Empty() )
				return ctx.ThrowException( "Missing array property" );

			red::DynArray< rtti::ValuePtr > newArrayValues{ red::PoolBackend() };
			for ( rtti::ValuePtr& arrayValue : arrayValues )
			{
				// not array
				if ( !arrayValue->IsArray() )
					return ctx.ThrowException( "Array operations can be performed only on arrays" );

				// out of range index ?
				if ( from >= arrayValue->GetNumElements() )
					return ctx.ThrowException( "'From' index is out of range (%d >= %d)", from, arrayValue->GetNumElements() );

				if ( to > arrayValue->GetNumElements() )
					return ctx.ThrowException( "'To' index is out of range (%d >= %d)", to, arrayValue->GetNumElements() );

				rtti::ValuePtr newArrayValue = rtti::ValueHolder::Parse( arrayValue->ToString().AsChar() );

				newArrayValue->InsertArrayElement( to, newArrayValue->GetArrayElement( from ) );

				if ( to < from )
					newArrayValue->RemoveArrayElement( from + 1 );
				else
					newArrayValue->RemoveArrayElement( from );

				newArrayValues.PushBack( newArrayValue );
			}

			// write it back
			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, arrayValues, newArrayValues ) )
			{
				return ctx.ThrowException( "Array operation failed" );
			}

			return true;
		}

		const Bool EditableCollectionArrayItem::ArraySwapElements( interop::IContext& ctx, const Uint32 from, const Uint32 to )
		{
			InteropRTTIContextAdapter adapter( ctx );

			red::DynArray< rtti::ValuePtr > arrayValues{ red::PoolBackend() };
			m_root->GetResolvedView()->GetCurrentValues( arrayValues, m_path );

			if ( arrayValues.Empty() )
				return ctx.ThrowException( "Missing array property" );

			red::DynArray< rtti::ValuePtr > newArrayValues{ red::PoolBackend() };
			for ( rtti::ValuePtr& arrayValue : arrayValues )
			{
				// not array
				if ( !arrayValue->IsArray() )
					return ctx.ThrowException( "Array operations can be performed only on arrays" );

				// out of range index ?
				if ( from >= arrayValue->GetNumElements() )
					return ctx.ThrowException( "'From' index is out of range (%d >= %d)", from, arrayValue->GetNumElements() );

				if ( to >= arrayValue->GetNumElements() )
					return ctx.ThrowException( "'To' index is out of range (%d >= %d)", to, arrayValue->GetNumElements() );

				// swap elements
				rtti::ValuePtr newArrayValue = rtti::ValueHolder::Parse( arrayValue->ToString().AsChar() );
				rtti::ValuePtr toValue = newArrayValue->GetArrayElement( to );
				newArrayValue->SetArrayElement( to, newArrayValue->GetArrayElement( from ) );
				newArrayValue->SetArrayElement( from, toValue );

				newArrayValues.PushBack( newArrayValue );
			}

			// write it back
			if ( !m_root->GetResolvedView()->ExecuteValueChange( adapter, m_path, arrayValues, newArrayValues ) )
			{
				return ctx.ThrowException( "Array operation failed" );
			}

			return true;
		}

		//----------------

		EditableCollectionStructItem::EditableCollectionStructItem( const EditableCollectionContext & context )	
			: m_collection( context.collection )
			, m_parentRoot( context.root )
			, m_path( context.path )
			, m_structType( static_cast< const rtti::ClassType* >( context.propertyInfo.m_type ) )
			, m_customTypeName( context.propertyInfo.m_customTypeName )
			, m_isReadOnly( context.propertyInfo.m_isReadOnly )
			, m_canReset( context.propertyInfo.m_canReset )
			, m_tooltip( context.propertyInfo.m_tooltip )
			, m_children{ red::PoolBackend() }
			, m_minValue( context.propertyInfo.m_minValue )
			, m_maxValue( context.propertyInfo.m_maxValue )
		{
			// get editable properties in class - this will be the same everywhere (in every view)
			rtti::EditableProperties properties;
			
			context.propertyInfo.m_isInstanceEditable ? 
				m_structType->GetInstanceEditableProperties( properties ) : 
				m_structType->GetEditableProperties( properties );

			// create wrappers
			for ( const auto& prop : properties.Get() )
			{
				auto propWrapper = CreatePropertyWrapper( m_collection, m_parentRoot, m_path[prop.m_name], prop );

				if ( propWrapper )
				{
					Child info;
					info.m_name = prop.m_name;
					info.m_object = propWrapper;
					info.m_category = prop.m_category;
					m_children.PushBack( info );
				}
			}

			m_parentRoot->RegisterItem( this );
		}

		EditableCollectionStructItem::~EditableCollectionStructItem()
		{
			m_parentRoot->UnregisterItem( this );
		}

		void EditableCollectionStructItem::GetChildrenInfo( interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo ) const
		{
			outInfo.Reserve( m_children.Size() );

			for ( const auto& info : m_children )
				outInfo.PushBack( interop::ChildWithCategory( info.m_name, info.m_category ) );
		}

		void EditableCollectionStructItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			String path = m_path.ToString();
			path.Replace( oldPath.ToString(), newPath.ToString() );
			m_path = rtti::AccessPath( path.AsChar() );

			for ( const Child& child : m_children )
			{
				child.m_object->Rebind( oldPath, newPath );
			}
		}

		interop::ObjectPtr EditableCollectionStructItem::GetChild( interop::IContext& ctx, CName name ) const
		{
			for ( const auto& info : m_children )
				if ( info.m_name == name )
					return info.m_object;

			return interop::ObjectPtr();
		}

		const Bool EditableCollectionStructItem::IsMultiValue() const
		{
			return m_parentRoot ? m_parentRoot->IsMultiValue( m_path ) : false;
		}

		const Uint8 EditableCollectionStructItem::IsDefault() const
		{
			Uint8 isDefault;
			if ( m_parentRoot->IsDefault( IRTTIContext::GetDefault(), m_path, isDefault ) )
			{
				return isDefault;
			}

			return 0;
		}

		const Bool EditableCollectionStructItem::IsReadOnly() const
		{
			return m_isReadOnly;
		}

		const CName EditableCollectionStructItem::GetType() const
		{
			if ( m_customTypeName ) 
				return m_customTypeName;
			else
				return m_structType->GetName();
		}

		const Bool EditableCollectionStructItem::ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const
		{
			// structure changed directly and fully
			rtti::PathParser parser( path );
			if ( !parser )
			{
				m_parentRoot->ProcessNotification( this );
				return true;
			}

			CName childName;
			if ( parser.EatName( childName ) )
			{
				interop::ObjectPtr object = GetChild( DummyContext(), childName );
				if ( object )
				{
					return object->ProcessNotification( parser.GetUneatenPath(), oldValue, targetValue );
				}
			}

			// not serviced
			return false;
		}

		interop::ObjectPtr EditableCollectionStructItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			using FunctorType = interop::utils::Functor1< void, CName >;

			if ( name == "_copy" )
			{
				return interop::ObjectPtr( RED_NEW( CopyPropertyItemFunctor )( m_parentRoot, m_path ) ); 
			}
			else if ( name == "_paste" )
			{
				return interop::ObjectPtr( RED_NEW( PastePropertyItemFunctor )( m_parentRoot, m_path ) );
			}
			else if ( name == "_copyCategory" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[ this ]( interop::IContext& ctx, const CName& categoryName )
				{
					CopyCategory( ctx, categoryName );
				}, interop::FunctorType::Const
					) );
			}
			else if ( name == "_pasteCategory" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[ this ]( interop::IContext& ctx, const CName& categoryName )
				{
					PasteCategory( ctx, categoryName );
				}
				) );
			}
			else if ( m_canReset && name == "_reset" )
			{
				return interop::ObjectPtr( RED_NEW( ResetFunctor )( m_parentRoot, m_path ) );
			}
			else if ( name == "_tooltip" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_tooltip ) );
			}

			if( name == "_min" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Float> )( m_minValue ) );
			}
			else if( name == "_max" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Float> )(m_maxValue) );
			}
			else if ( name == "_id" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< proxy::GenericId > )( m_parentRoot->GetItemId( this ) ) );
			}

			return interop::ICompoundObject::GetUnlistedChild( ctx, name );
		}

		void EditableCollectionStructItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr value;
			if ( !m_parentRoot->ReadValue( adapter, m_path, value ) )
			{
				ctx.ThrowException( "Unable to read value from collection root" );
				return;
			}

			outValue = value->ToString();
		}

		void EditableCollectionStructItem::Set( interop::IContext& ctx, const interop::InteropString& newValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr newValuePtr = rtti::ValueHolder::Parse( newValue.AsChar() );
			if ( !newValuePtr )
			{
				ctx.ThrowException( "Unable to parse new value" );
				return;
			}

			if ( !m_parentRoot->WriteValue( adapter, m_path, newValuePtr ) )
			{
				ctx.ThrowException( "Unable to write new value" );
				return;
			}
		}

		void EditableCollectionStructItem::CopyCategory( interop::IContext& ctx, const CName& categoryName ) const
		{
			red::DynArray< rtti::AccessPath > childrenToCopy{ red::PoolBackend() };
			for ( const Child& child : m_children )
			{
				if ( child.m_category == categoryName )
					childrenToCopy.PushBack( m_path[ child.m_name ] );
			}

			THandle< CategoryClipboardHolder > holder = CreateHandle< CategoryClipboardHolder >( categoryName, childrenToCopy );
			CopyProperties( ctx, m_parentRoot, holder );
		}

		void EditableCollectionStructItem::PasteCategory( interop::IContext& ctx, const CName& categoryName ) const
		{
			red::DynArray< rtti::AccessPath > childrenToCopy{ red::PoolBackend() };
			for ( const Child& child : m_children )
			{
				if ( child.m_category == categoryName )
					childrenToCopy.PushBack( m_path[ child.m_name ] );
			}

			PasteProperties( ctx, m_parentRoot, childrenToCopy );
		}

		//----------------

		EditableCollectionRootItem::EditableCollectionRootItem( EditableCollection* collection, EditableCollectionRootItem* parentRoot )
			: m_parentRoot( parentRoot )
			, m_resolvedView( red::CreateSharedPtr< ResolvedView >( collection ) )
			, m_children{ red::PoolBackend() }
		{
			m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{
				OnNotifyPropertyChange( id, path, oldValue, targetValue );
			} );

			if ( m_parentRoot )
			{
				m_dataControlBackend = m_parentRoot->GetDataControlBackend();
			}
			else
			{
				m_dataControlBackend = red::CreateSharedPtr< EditableCollectionDataControlBackend >( this );
				m_dataControlInterop = red::CreateUniquePtr< DataControlInterop >( m_dataControlBackend );
			}

			if ( m_parentRoot )
			{
				RegisterItem( this );
			}
		}

		EditableCollectionRootItem::~EditableCollectionRootItem()
		{
			UnregisterItem( this );
		}

		void EditableCollectionRootItem::SetEditableView( red::SharedPtr< IEditableView >& view )
		{
			red::DynArray< red::SharedPtr< IEditableView > > views{ red::PoolBackend() };
			views.Reserve(1);

			if ( view )
				views.PushBack( view );

			SetEditableViews( views );
		}

		void EditableCollectionRootItem::SetEditableViews( const red::DynArray< red::SharedPtr< IEditableView > >& views )
		{
			m_resolvedView->SetEditableViews( views );
		
			RefreshView( true );
		}

		void EditableCollectionRootItem::GetChildrenInfo( interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo ) const
		{
			outInfo.Reserve( m_children.Size() );

			for ( const auto& info : m_children )
				outInfo.PushBack( interop::ChildWithCategory( info.m_name, info.m_category ) );
		}

		interop::ObjectPtr EditableCollectionRootItem::GetChild( interop::IContext& ctx, CName name ) const
		{
			for ( const auto& info : m_children )
			{
				if ( info.m_name == name )
				{
					return info.m_object;
				}
			}

			return interop::ObjectPtr();
		}

		interop::ObjectPtr EditableCollectionRootItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			using FunctorType = interop::utils::Functor1< void, CName >;

			if ( name == "_copyCategory" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[this]( interop::IContext& ctx, const CName& categoryName )
					{
						CopyCategory( ctx, categoryName );
					}, interop::FunctorType::Const
				) );
			}
			else if ( name == "_pasteCategory" )
			{
				return interop::ObjectPtr( RED_NEW( FunctorType )(
					[this]( interop::IContext& ctx, const CName& categoryName )
					{
						PasteCategory( ctx, categoryName );
					}
				) );
			}
			else if ( m_dataControlInterop && name == "_data" )
			{
				return interop::ObjectPtr( m_dataControlInterop->CreateInterop() );
			}
			else if ( name == "_id" )
			{
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< proxy::GenericId > )( GetItemId( this ) ) );
			}

			return interop::ObjectPtr();
		}

		const CName EditableCollectionRootItem::GetType() const
		{
			return m_propertyType ? m_propertyType : m_resolvedView->GetViewClass();
		}

		const CName EditableCollectionRootItem::GetViewClass() const
		{
			return m_resolvedView->GetViewClass();
		}

		const red::SharedPtr< IEditableView > EditableCollectionRootItem::GetNegotiatedView() const
		{
			return m_resolvedView->GetNegotiatedView();
		}

		void EditableCollectionRootItem::RegisterItem( interop::IBaseObject* item )
		{
			m_dataControlBackend->RegisterItem( item );
		}

		void EditableCollectionRootItem::UnregisterItem( interop::IBaseObject* item )
		{
			m_dataControlBackend->UnregisterItem( item );
		}

		interop::IBaseObject* EditableCollectionRootItem::GetItemById( const proxy::GenericId id )
		{
			return m_dataControlBackend->GetItemById( id );
		}

		proxy::GenericId EditableCollectionRootItem::GetItemId( const interop::IBaseObject* item ) const
		{
			return m_dataControlBackend->GetItemId( item );
		}

		void EditableCollectionRootItem::NotifyItemCreated( interop::IBaseObject* parent, interop::IBaseObject* item, const Uint32 index )
		{
			m_dataControlBackend->OnItemCreated( parent, item, index );
		}

		void EditableCollectionRootItem::NotifyItemRemoved( interop::IBaseObject* item )
		{
			m_dataControlBackend->OnItemRemoved( item );
		}

		void EditableCollectionRootItem::NotifyItemMoved( interop::IBaseObject* item, const Uint32 index )
		{
			m_dataControlBackend->OnItemMoved( item, index );
		}

		const Bool EditableCollectionRootItem::IsReadOnly() const
		{
			return m_resolvedView->IsReadOnly();
		}

		const Bool EditableCollectionRootItem::IsMultiValue( const rtti::AccessPath& propertyPath ) const
		{
			return m_resolvedView->IsMultiValue( propertyPath );
		}

		const Bool EditableCollectionRootItem::ReadValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, rtti::ValuePtr& outValue ) const
		{
			return m_resolvedView->GetValue( ctx, accessPath, outValue );
		}

		const Bool EditableCollectionRootItem::WriteValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const rtti::ValuePtr& newValue,
			const red::DynArray< rtti::ValuePtr >& oldValues ) const
		{
			return m_resolvedView->ExecuteValueChange( ctx, accessPath, oldValues, newValue );
		}

		const Bool EditableCollectionRootItem::WriteValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const red::DynArray< rtti::ValuePtr >& newValues,
			const red::DynArray< rtti::ValuePtr >& oldValues ) const
		{
			return m_resolvedView->ExecuteValueChange( ctx, accessPath, oldValues, newValues );
		}

		const Bool EditableCollectionRootItem::ResetValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const red::DynArray< rtti::ValuePtr >& oldValues) const
		{
			return m_resolvedView->ExecuteValueChange( ctx, accessPath, oldValues );
		}

		const Bool EditableCollectionRootItem::IsDefault( IRTTIContext& ctx, const rtti::AccessPath& accessPath, Uint8& outValue ) const
		{
			if ( m_resolvedView->IsEmpty() )
			{
				ctx.ReportError( nullptr, "No objects bound to root item" );
				return false;
			}
			return m_resolvedView->IsDefault( ctx, accessPath, outValue );
		}

		const Bool EditableCollectionRootItem::HasObject(const SerializableID& id) const
		{
			return m_resolvedView->HasObject( id );
		}

		void EditableCollectionRootItem::RefreshView( Bool fullRefresh )
		{
			// uncomment this line to find regression bugs after CL#2956573
			//if ( fullRefresh )
			{
				m_children.Clear();
			}

			// determine bast view class
			if ( !m_resolvedView->IsEmpty() )
			{
				// get properties
				rtti::EditableProperties properties;
				m_resolvedView->GetRootProperties( properties );

				red::DynArray< Uint32 > toRemove;
				for ( Uint32 i = 0; i < m_children.Size(); ++i )
				{
					if ( properties.IndexOf( m_children[i].m_name ) == red::INVALID_INDEX )
					{
						toRemove.PushBack( i );
					}
				}

				for ( const Uint32& index : toRemove.Reverse() )
				{
					m_children.RemoveAt( index );
				}

				red::DynArray< CName > childrenNames;
				for ( const Child& child : m_children )
				{
					childrenNames.PushBack( child.m_name );
				}

				red::DynArray< interop::ObjectPtr > createdWrappers;
				red::DynArray< Uint32 > propIndices;

				// create wrappers
				Uint32 propIndex = 0;
				for ( const auto& prop : properties.Get() )
				{
					Int32 index = childrenNames.GetIndex( prop.m_name );
					if ( index < 0 )
					{
						rtti::AccessPath rootPath;

						auto propWrapper = CreatePropertyWrapper( m_resolvedView->GetEditableCollection(), this, rootPath[prop.m_name], prop );

						if ( propWrapper )
						{
							Child info;
							info.m_name = prop.m_name;
							info.m_object = propWrapper;
							info.m_category = prop.m_category;
							if ( propIndex < m_children.Size() )
							{
								m_children.InsertAt( propIndex, info );
							}
							else
							{
								m_children.PushBack( info );
							}

							if ( !childrenNames.Empty() )
							{
								createdWrappers.PushBack( propWrapper );
								propIndices.PushBack( propIndex );
							}
						}
					}
					else
					{
						ProcessNotification( m_children[index].m_object.Get() );
					}
					propIndex++;
				}
				
				if ( !createdWrappers.Empty() )
				{
					//m_dataControlBackend->OnItemChanged( this );
					m_dataControlBackend->GetBranchIds( m_dataControlBackend->GetItemId( this ) );
				}

				for ( Uint32 i = 0; i < createdWrappers.Size(); ++i )
				{
					NotifyItemCreated( this, createdWrappers[i].Get(), propIndices[i] );
				}
			}
			else
			{
				m_children.Clear();
			}

			ProcessNotification( this );
		}

		void EditableCollectionRootItem::ProcessNotification( const interop::IBaseObject* object ) const
		{
			if ( m_dataControlBackend )
			{
				m_dataControlBackend->OnItemChanged( object );
			}
		}

		const Bool EditableCollectionRootItem::ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const
		{
			rtti::PathParser parser( path );

			// at root scope we only check if a sub-element is being changed
			CName childName;
			if ( parser.EatName(childName) )
			{
				// look for child to notify
				for ( const auto& info : m_children )
				{
					if ( info.m_name == childName )
					{
						// allow the child to process the notification
						return info.m_object->ProcessNotification( parser.GetUneatenPath(), oldValue, targetValue );
					}
				}				
			}

			// never propagate updates
			return false;
		}

		void EditableCollectionRootItem::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			// is this anything for this root ?
			// TODO: if we have a lot of root this may be very slow to check
			if ( HasObject( id ) )
			{
				if ( path.IsEmpty() )
				{
					RefreshView( false );
				}
				else
				{
					ProcessNotification( path, oldValue, targetValue );
				}
			}
		}

		red::SharedPtr< EditableCollectionDataControlBackend > EditableCollectionRootItem::GetDataControlBackend()
		{
			return m_dataControlBackend;
		}

		void EditableCollectionRootItem::CopyCategory( interop::IContext& ctx, const CName& categoryName ) const
		{
			red::DynArray< rtti::AccessPath > childrenToCopy{ red::PoolBackend() };
			for ( const Child& child : m_children )
			{
				if ( child.m_category == categoryName )
					childrenToCopy.PushBack( rtti::AccessPath()[ child.m_name ] );
			}

			THandle< CategoryClipboardHolder > holder = CreateHandle< CategoryClipboardHolder >( categoryName, childrenToCopy );
			CopyProperties( ctx, this, holder );
		}

		void EditableCollectionRootItem::PasteCategory( interop::IContext& ctx, const CName& categoryName ) const
		{
			red::DynArray< rtti::AccessPath > childrenToCopy{ red::PoolBackend() };
			for ( const Child& child : m_children )
			{
				if ( child.m_category == categoryName )
					childrenToCopy.PushBack( rtti::AccessPath()[ child.m_name ] );
			}

			PasteProperties( ctx, const_cast< EditableCollectionRootItem* >( this ), childrenToCopy );
		}

		//----------------
		class ResetPointerFunctor : public interop::IFunctorObject
		{
		public:
			ResetPointerFunctor( const EditableCollectionInlinedRootItem* parent )
				: m_parent( parent )
			{}

			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const
			{
				// no reported params
			}

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const
			{
				m_parent->ResetValue( ctx );
				return interop::InteropString( "" );
			}

			virtual const CName GetType() const override final
			{
				return CName::NONE(); // void return type
			}

		private:
			const EditableCollectionInlinedRootItem* m_parent;
		};

		EditableCollectionInlinedRootItem::EditableCollectionInlinedRootItem( const EditableCollectionContext & context )
			: EditableCollectionRootItem( context.collection, context.root )
			, m_path( context.path )
			, m_isReadOnly( context.propertyInfo.m_isReadOnly )
			, m_canClear( context.propertyInfo.m_canClear )
			, m_canReset( context.propertyInfo.m_canReset )
			, m_canSelect( context.propertyInfo.m_canSelect )
			, m_tooltip( context.propertyInfo.m_tooltip )
			, m_keyPath( context.propertyInfo.m_keyPath )
		{

			m_propertyType = RED_NAME( String::Printf( "Inlined:%hs", context.propertyInfo.m_type->GetName().AsChar() ).AsChar() );
			m_propertyType = GetPropertyItemTypeName( m_propertyType, m_keyPath );
			
			// 22.01.18 WC. I'm disabling this for now, there is one case (but can be more) in which we have a custom value box for
			// inlined object and it would be nice if it was resetable by MMB click

			/*if ( m_canReset && m_canClear )
			{
				// check default value - if it's NULL there is no point in having both reset and clear enabled
				CName parentClassName = m_parentRoot->GetViewClass();
				if ( const rtti::ClassType* parentClass = GetRttiSystem().FindClass( parentClassName ) )
				{
					ISerializable* defaultObject = parentClass->GetDefaultObject<ISerializable>();

					rtti::ValuePtr defaultValue;
					if ( !defaultObject
						 || !parentClass->ReadValue( IRTTIContext::GetDefault(), defaultObject, rtti::AccessPath( context.propertyInfo.m_name.AsChar() ), defaultValue )
						 || defaultValue->IsEmpty() )
					{
						m_canReset = false;
					}
				}
			}*/
		}

		void EditableCollectionInlinedRootItem::Get( interop::IContext& ctx, interop::InteropString& outValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			rtti::ValuePtr value;
			if ( !m_parentRoot->ReadValue( adapter, m_path, value ) )
			{
				ctx.ThrowException( "Unable to read value from collection root" );
				return;
			}

			outValue = value->ToString();
		}

		void EditableCollectionInlinedRootItem::Set( interop::IContext& ctx, const interop::InteropString& newValue ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			auto newValuePtr = rtti::ValueHolder::Parse( newValue.AsChar() );
			if ( !newValuePtr )
			{
				ctx.ThrowException( "Unable to parse new value" );
				return;
			}

			// get current value for each view
			red::DynArray< rtti::ValuePtr > oldValues{ red::PoolBackend() };
			m_resolvedView->GetCurrentValues( oldValues );

			if ( !m_parentRoot->WriteValue( adapter, m_path, newValuePtr, oldValues ) )
			{
				ctx.ThrowException( "Unable to write new value" );
				return;
			}
		}

		interop::ObjectPtr EditableCollectionInlinedRootItem::GetUnlistedChild( interop::IContext& ctx, const String& name ) const
		{
			if ( name == "_class" )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<String> )( m_resolvedView->GetViewClass().AsChar() ) );
			else if ( name == "_canClear" )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_canClear ) );
			else if ( name == "_canReset" )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_canReset ) );
			else if ( name == "_canSelect" )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject<Bool> )( m_canSelect ) );
			else if ( m_canReset && name == "_reset" )
				return interop::ObjectPtr( RED_NEW( ResetPointerFunctor )( this ) );
			else if ( name == "_copy" )
				return interop::ObjectPtr( RED_NEW( CopyPropertyItemFunctor )( m_parentRoot, m_path ) );
			else if ( name == "_paste" )
				return interop::ObjectPtr( RED_NEW( PastePropertyItemFunctor )( m_parentRoot, m_path ) );
			else if ( name == "_tooltip" )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_tooltip ) );
			else if ( name == "_keyPath" && !m_keyPath.Empty() )
				return interop::ObjectPtr( RED_NEW( interop::utils::ConstValueObject< String > )( m_keyPath ) );

			return EditableCollectionRootItem::GetUnlistedChild( ctx, name );
		}

		const Bool EditableCollectionInlinedRootItem::RebuildViews()
		{
			// read objects pointers
			red::DynArray< rtti::ValuePtr > currentValues{ red::PoolBackend() };
			m_parentRoot->GetResolvedView()->GetCurrentValues( currentValues, m_path );

			const red::DynArray< red::SharedPtr< IEditableView > >& parentViews = m_parentRoot->GetResolvedView()->GetViews();
			red::DynArray< red::SharedPtr< IEditableView > > views{ red::PoolBackend() };

			Uint32 counter = 0;
			for ( rtti::ValuePtr& propValue : currentValues )
			{
				red::SharedPtr< IEditableView > view;

				// expected single value that is an object
				if ( propValue && ( propValue->IsHandle() || propValue->IsEmpty() ) )
				{
					// extract object
					THandle< ISerializable > objectPtr = nullptr;
					if ( GetTypeObject< THandle< ISerializable > >()->WriteValue( IRTTIContext::GetDefault(),
						&objectPtr, rtti::AccessPath(), *propValue ) && objectPtr )
					{
						// parent view
						tools::EditableViewPtr parentView = parentViews[counter];

						// get the view from the serializable
						view = objectPtr->CreateView( parentView, m_isReadOnly, CName::NONE(), parentView->GetUserContext() );
					}
				}

				views.PushBack( view );
				counter++;
			}

			SetEditableViews( views );
			return true;
		}

		void EditableCollectionInlinedRootItem::ResetValue( interop::IContext& ctx ) const
		{
			InteropRTTIContextAdapter adapter( ctx );
			
			red::DynArray< rtti::ValuePtr > oldValues{ red::PoolBackend() };
			m_resolvedView->GetCurrentValues( oldValues );

			if ( !m_parentRoot->ResetValue( adapter, m_path, oldValues ) )
			{
				ctx.ThrowException( "Unable to write new value" );
			}
		}

		void EditableCollectionInlinedRootItem::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
		{
			if ( ( m_parentRoot->HasObject( id ) && path.ToString().EqualsNC( m_path.ToString() ) ) ||
				 ( HasObject( id ) && path.IsEmpty() ) )
			{
				if ( !RebuildViews() )
					SetEditableViews( red::DynArray< red::SharedPtr< IEditableView > >( red::PoolBackend() ) );
			}
			EditableCollectionRootItem::OnNotifyPropertyChange( id, path, oldValue, targetValue );
		}

		const Bool EditableCollectionInlinedRootItem::IsMultiValue() const
		{
			return m_resolvedView->IsMultiValue( rtti::AccessPath() );
		}

		void EditableCollectionInlinedRootItem::Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath )
		{
			String path = m_path.ToString();
			path.Replace( oldPath.ToString(), newPath.ToString() );
			m_path = rtti::AccessPath( path.AsChar() );
		}

		//----------------

		template< typename PtrType >
		interop::ObjectPtr CreateInlinedRoot( const EditableCollectionContext & context )
		{

			// must be at least serializable
			if ( static_cast< const rtti::ClassType* >( context.propertyInfo.m_type )->IsA< ISerializable >() )
			{
				// create the root view
				auto* rootItem = RED_NEW( EditableCollectionInlinedRootItem )( context );
				rootItem->RebuildViews();
				return interop::ObjectPtr( rootItem );
			}

			return interop::ObjectPtr();
		}

		interop::ObjectPtr CreatePropertyWrapper( EditableCollection* collection, EditableCollectionRootItem* root, const rtti::AccessPath& pathSoFar, const rtti::ClassEditablePropertyInfo& prop )
		{
			EditableCollectionContext context =
			{
				collection,
				root,
				pathSoFar,
				prop
			};
			
			// Search for custom property wrappers
			CName customTypeName;
			if ( prop.m_customTypeMode == rtti::CustomEditorMode::NormalType )
				customTypeName = prop.m_customTypeName;                         // Set it here to avoid mode checks in the code below.

#ifndef RED_CONFIGURATION_FINAL 
			if( customTypeName.Empty() )
			{
				// Search for custom type wrappers
				if( prop.m_type->GetType() == RT_Class )
				{
					const rtti::ClassType* classType = static_cast< const rtti::ClassType* >( prop.m_type );
					customTypeName = classType->GetCustomEditorName();
				}
			}
#endif	//RED_CONFIGURATION_FINAL

			if( !customTypeName.Empty() )
			{
				red::SharedPtr<IEditableCustomProperty> customEditor( IEditableCustomProperty::CreateCustomEditor( customTypeName,
					collection, root, pathSoFar, prop.m_type, prop.m_isInlined, prop.m_isReadOnly ) );
				if ( customEditor )
				{
					if ( customEditor->IsCompound() )
					{
						return interop::ObjectPtr( RED_NEW( EditableCollectionCustomStructItem )( customEditor, context ) );
					}
					return interop::ObjectPtr( RED_NEW( EditableCollectionCustomPropertyItem )( customEditor, pathSoFar ) );
				}
			}

			// Search for standard wrappers
			switch ( prop.m_type->GetType() )
			{
				case RT_Simple:
				case RT_Name:
				case RT_Fundamental:
				case RT_LegacySingleChannelCurve:
				{
					if ( prop.m_maxValue > prop.m_minValue || prop.m_xMaxValue > prop.m_xMinValue )
					{
						context.propertyInfo.m_customTypeName = GetRangedTypeName( prop.m_type->GetName() );
						return interop::ObjectPtr( RED_NEW( EditableCollectionRangedPropertyItem )( context ) );
					}
					else
						return interop::ObjectPtr( RED_NEW( EditableCollectionPropertyItem )( context ) );
				}
				case RT_Enum:
				{
					context.propertyInfo.m_customTypeName = GetEnumTypeName();
					return interop::ObjectPtr( RED_NEW( EditableCollectionEnumPropertyItem )( context ) );
				}
				case RT_BitField:
				{
					context.propertyInfo.m_customTypeName = GetBitFieldTypeName( prop.m_isMask );
					return interop::ObjectPtr( RED_NEW( EditableCollectionBitfieldPropertyItem )( context ) );
				}
				case RT_ResourceReference:
				{
					const auto* pointerType = static_cast< const rtti::ResourceReferenceType* >( prop.m_type );
					RED_FATAL_ASSERT( pointerType->GetPointedType()->IsA< CResource >(), "Resource reference can contain only resource." );
					return interop::ObjectPtr( RED_NEW( EditableCollectionPropertyItem )( context ) );
				}
				case RT_ResourceAsyncReference:
				{
					const auto* pointerType = static_cast< const rtti::ResourceAsyncReferenceType* >( prop.m_type );
					RED_FATAL_ASSERT( pointerType->GetPointedType()->IsA< CResource >(), "Resource reference can contain only resource." );
					return interop::ObjectPtr( RED_NEW( EditableCollectionPropertyItem )( context ) );
				}
				case RT_Handle:
				case RT_Pointer:
				{
					const auto* pointerType = static_cast< const rtti::IBasePointerType* >( prop.m_type );

					if ( prop.m_isInlined || prop.m_isReadOnly )
					{
						context.propertyInfo.m_type = pointerType->GetPointedType();
						return CreateInlinedRoot< ISerializable* >( context );
					}
					else
					{
						if( prop.m_isReadOnly)
						{
							RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Interop, "Collection: Property '%hs' of type '%hs' cannot be displayed in editable collection because it is a pointer type and is marked as read only",
								pathSoFar.ToString().AsChar(), prop.m_type->GetName().AsChar() );
						}
						else
						{
							RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Interop, "Collection: Property '%hs' of type '%hs' cannot be displayed in editable collection because it is a pointer type and is NOT marked as inlined",
								pathSoFar.ToString().AsChar(), prop.m_type->GetName().AsChar() );
						}
					}
					
					// no way to display this
					return interop::ObjectPtr();
				}

				case RT_Array:
				case RT_NativeArray:
				case RT_StaticArray:
				{
					const auto* arrayType = static_cast< const rtti::IBaseArrayType* >( prop.m_type );
					return interop::ObjectPtr( RED_NEW( EditableCollectionArrayItem )( context ) );
				}

				case RT_Class:
				{
					const auto* classType = static_cast< const rtti::ClassType* >( prop.m_type );
					return interop::ObjectPtr( RED_NEW( EditableCollectionStructItem )( context ) );
				}
			}

			return interop::ObjectPtr();
		}

		ResetFunctor::ResetFunctor( const EditableCollectionRootItem* parent, const rtti::AccessPath& path ) : m_root( parent )
			, m_path( path )
		{
		}

		void ResetFunctor::GetParamTypes( interop::IContext&, red::DynArray< CName >& ) const
		{
			// no reported params
		}

		interop::InteropString ResetFunctor::Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const
		{
			red::SharedPtr< ResolvedView > resolvedView = m_root->GetResolvedView();
			red::DynArray< rtti::ValuePtr > oldValues{ red::PoolBackend() };

			resolvedView->GetCurrentValues( oldValues, m_path );
			m_root->ResetValue( InteropRTTIContextAdapter( ctx ), m_path, oldValues );

			return interop::InteropString::EMPTY();
		}

		const CName ResetFunctor::GetType() const
		{
			return CName::NONE(); // void return type
		}

	} // prv
} // tools