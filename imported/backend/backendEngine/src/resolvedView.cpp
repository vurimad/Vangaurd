#include "build.h"
#include "resolvedView.h"

#include "../../../common/redReflection/include/editableView.h"
#include "editableCollection.h"

namespace tools
{
	Bool AreEqual( const rtti::ClassEditablePropertyInfo& propInfoA, const rtti::ClassEditablePropertyInfo& propInfoB )
	{
		return propInfoA.m_name == propInfoB.m_name && propInfoA.m_type == propInfoB.m_type
			&& propInfoA.m_category == propInfoB.m_category && propInfoA.m_customTypeName == propInfoB.m_customTypeName;
	}

	ResolvedView::ResolvedView( EditableCollection* collection )
		: m_collection( collection )
		, m_views{ red::PoolBackend() }
	{
	}

	void ResolvedView::SetEditableViews( const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		m_views = views;
		m_viewClass = views.Empty() || !m_views.Back() ? CName::NONE() : m_views.Back()->GetViewClass()->GetName();
	}

	void ResolvedView::GetRootProperties( rtti::EditableProperties& outRootProperties ) const
	{
		rtti::EditableProperties props;

		if ( m_views[0] )
		{
			m_views[0]->GetRootProperties( props );
		}

		for ( Uint32 i = 0; i < m_views.Size(); ++i )
		{
			if ( !m_views[i] )
			{
				props.Clear();
				break;
			}

			rtti::EditableProperties currViewProps;
			m_views[i]->GetRootProperties( currViewProps );

			for ( Int32 propInd = props.Get().Size() - 1; propInd >= 0; --propInd )
			{
				const rtti::ClassEditablePropertyInfo& propInfo = props.Get()[propInd];

				Bool result = currViewProps.RemoveIf( [&propInfo]( const rtti::ClassEditablePropertyInfo& currPropInfo )
				{
					return AreEqual( propInfo, currPropInfo );
				} );

				if ( !result )
				{
					props.RemoveAt( propInd );
				}
			}
		}

		outRootProperties.Add( props.Get() );
	}

	Bool ResolvedView::IsReadOnly() const
	{
		for ( const auto& view : m_views )
		{
			if ( view && view->IsReadOnly() )
			{
				return true;
			}
		}
		return false;
	}

	Bool ResolvedView::IsReadOnly( const rtti::AccessPath& path ) const
	{
		for ( const red::SharedPtr< IEditableView >& view : m_views )
		{
			Bool isReadOnly = false;
			if ( view && view->IsReadOnly( IRTTIContext::GetDefault(), path, isReadOnly ) && isReadOnly )
			{
				return true;
			}
		}
		return false;
	}

	Bool ResolvedView::IsEmpty() const
	{
		return m_views.Empty();
	}

	Bool ResolvedView::IsMultiValue( const rtti::AccessPath& path ) const
	{
		if ( IsEmpty() )
		{
			return false;
		}

		if ( path.IsEmpty() )
		{
			for ( const red::SharedPtr< IEditableView >& view : m_views )
			{
				if ( view )
				{
					if ( view->GetViewClass()->GetName() != m_viewClass )
					{
						return true;
					}
				}
				else if ( m_viewClass != CName::NONE() )
				{
					return true;
				}
			}
		}

		rtti::ValuePtr testValue;
		if ( m_views[0] && !m_views[0]->GetValue( IRTTIContext::GetDefault(), path, testValue ) )
		{
			return false;
		}

		rtti::ValuePtr checkValue;
		for ( Uint32 i = 1; i < m_views.Size(); ++i )
		{
			if ( m_views[i] && !m_views[i]->GetValue( IRTTIContext::GetDefault(), path, checkValue ) )
			{
				return true;
			}

			if ( checkValue != nullptr && testValue != nullptr )
			{
				if ( checkValue->ToString() != testValue->ToString() )
				{
					return true;
				}
			}
			else if ( checkValue != testValue )
			{
				return true;
			}
		}
		return false;
	}

	Bool ResolvedView::IsDefault( IRTTIContext& ctx, const rtti::AccessPath& accessPath, Uint8& outValue ) const
	{
		for ( auto& view : m_views )
		{
			if ( view && !view->IsDefault( ctx, accessPath, outValue ) )
			{
				outValue = 0;
				return false;
			}
		}
		return true;
	}

	Bool ResolvedView::GetValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, rtti::ValuePtr& outValue ) const
	{
		if ( m_views.Empty() )
		{
			return ctx.ReportError( nullptr, "No objects bound to root item" );
		}

		rtti::ValuePtr value;
		if ( !m_views.Back() || !m_views.Back()->GetValue( ctx, accessPath, value ) || !value )
		{
			return ctx.ReportError( nullptr, "Cannot read value" );
		}

		if ( value->IsArray() )
		{
			for ( Uint32 i = 0; i < m_views.Size() - 1; ++i )
			{
				rtti::ValuePtr testValue;
				if ( m_views[i] && !m_views[i]->GetValue( ctx, accessPath, testValue ) )
				{
					return ctx.ReportError( nullptr, "Cannot read value" );
				}

				if ( !testValue->IsArray() )
				{
					return ctx.ReportError( nullptr, "Types mismatch" );
				}

				if ( testValue->GetNumElements() < value->GetNumElements() )
				{
					value = testValue;
				}
			}
		}

		outValue = value;
		return true;
	}

	void ResolvedView::GetCurrentValues( red::DynArray<rtti::ValuePtr>& outValues, const rtti::AccessPath& path ) const
	{
		outValues.Clear();
		outValues.Reserve( m_views.Size() );

		for ( const auto& view : m_views )
		{
			rtti::ValuePtr curValue;
			if ( view && view->GetValue( IRTTIContext::GetDefault(), path, curValue ) && curValue && curValue->IsStructure() )
			{
				curValue->SetStructElement( RED_NAME_CONSTEXPR( "class" ), rtti::ValueHolder::Parse( m_viewClass.AsChar() ) );
			}
			outValues.PushBack( curValue );
		}
	}

	Bool ResolvedView::ExecuteValueChange( IRTTIContext& ctx, const rtti::AccessPath& accessPath,
		const red::DynArray< rtti::ValuePtr >& oldValues, const rtti::ValuePtr& newValue ) const
	{
		RED_ASSERT( oldValues.Empty() || oldValues.Size() == m_views.Size(), "Overridden old values for %hs have different size than available views collection",
			accessPath.ToString().AsChar() );

		EditableCollection::ExecuteData data;
		data.m_newValue = newValue;
		data.m_path = accessPath;

		// get current value for each view
		data.m_oldValues.Reserve( m_views.Size() );
		for ( Uint32 i = 0; i < m_views.Size(); ++i )
		{
			const auto& view = m_views[i];

			if ( i < oldValues.Size() && oldValues[i] )
			{
				data.m_oldValues.PushBack( oldValues[i] );
				data.m_views.PushBack( view );
			}
			else
			{
				rtti::ValuePtr curValue;
				if ( !view->GetValue( IRTTIContext::GetDefault(), accessPath, curValue ) )
				{
					return ctx.ReportError( nullptr, "Can't get the current value, probably view is out of sync with data" );
				}

				data.m_oldValues.PushBack( curValue );
				data.m_views.PushBack( view );
			}
		}

		// nothing to set
		if ( data.m_views.Empty() )
			return ctx.ReportError( nullptr, "There are no valid views to set the value on" );

		// execute
		if ( m_collection->ExecuteValueChange( data ) )
			return true;

		// final error
		return ctx.ReportError( nullptr, "Failed to set the new value" );
	}

	Bool ResolvedView::ExecuteValueChange( IRTTIContext& ctx, const rtti::AccessPath& accessPath,
		const red::DynArray< rtti::ValuePtr >& oldValues, const red::DynArray< rtti::ValuePtr >& newValues ) const
	{
		RED_ASSERT( oldValues.Empty() || oldValues.Size() == m_views.Size(), "Overridden old values for %hs have different size than available views collection",
			accessPath.ToString().AsChar() );

		for ( Uint32 i = 0; i < m_views.Size(); ++i )
		{
			const auto& view = m_views[i];
			if ( !view )
			{
				continue;
			}

			EditableCollection::ExecuteData data;
			data.m_newValue = newValues[i];
			data.m_path = accessPath;

			if ( i < oldValues.Size() && oldValues[i] )
			{
				data.m_oldValues.PushBack( oldValues[i] );
				data.m_views.PushBack( view );
			}
			else
			{
				rtti::ValuePtr curValue;
				if ( view->GetValue( IRTTIContext::GetDefault(), accessPath, curValue ) && curValue )
				{
					data.m_oldValues.PushBack( curValue );
				}
				data.m_views.PushBack( view );
			}

			// execute
			if ( !m_collection->ExecuteValueChange( data ) )
				return ctx.ReportError( nullptr, "Failed to set the new value" );
		}

		return true;
	}

	Bool ResolvedView::HasObject( const SerializableID& id ) const
	{
		for ( const auto& view : m_views )
		{
			if ( view && view->GetSerializableID() == id )
			{
				return true;
			}
		}
		return false;
	}

	const red::SharedPtr< IEditableView > ResolvedView::GetNegotiatedView() const
	{
		if ( m_views.Size() > 0 )
			return m_views.Back();

		return red::SharedPtr< IEditableView >();
	}
}