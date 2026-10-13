/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "editableCollection.h"

#include "editableProxy.h"
#include "../../../common/redReflection/include/editableView.h"

namespace tools
{

	EditableCollection::EditableCollection()
		: m_root( RED_NEW( prv::EditableCollectionRootItem )( this, nullptr ) )
	{		
	}

	EditableCollection::~EditableCollection()
{
	}

	void EditableCollection::BindExecutor( const TExecutor& executor )
	{
		m_executor = executor;
	}

	void EditableCollection::BindTransactionHandler( const TTransactionHandler& transactionHandler )
	{
		m_transactionHandler = transactionHandler;
	}

	const Bool EditableCollection::ExecuteValueChange( const ExecuteData& data ) const
	{
		if ( !m_executor )
			return false;

		return m_executor( data );
	}

	void EditableCollection::BeginTransaction()
	{
		if ( m_transactionHandler )
		{
			m_transactionHandler( true );
		}
	}

	void EditableCollection::EndTransaction()
	{
		if ( m_transactionHandler )
		{
			m_transactionHandler( false );
		}
	}

	void EditableCollection::Clear()
	{
		red::DynArray< red::SharedPtr< IEditableView > > views;
		m_root->SetEditableViews( views );
	}

	void EditableCollection::SetObject( const red::SharedPtr< IEditableView >& view )
	{
		red::DynArray< red::SharedPtr< IEditableView > > views;
		if ( view )
			views.PushBack( view );

		m_root->SetEditableViews( views );
	}

	void EditableCollection::SetObject( const red::SharedPtr< IEditableProxy >& proxy )
	{
		if ( proxy )
		{
			auto view = proxy->CreateView( proxy->GetViewClass() );
			SetObject( view );
		}
		else
		{
			Clear();
		}
	}

	void EditableCollection::SetObject( const THandle< ISerializable >& object )
	{
		if ( object )
		{
			auto view = object->CreateView();
			SetObject( view );
		}
		else
		{
			Clear();
		}
	}

	void EditableCollection::SetObjects( const red::DynArray< red::SharedPtr< IEditableView > >& views )
	{
		m_root->SetEditableViews( views );
	}

	Bool EditableCollection::HasObject( const SerializableID& id ) const
	{
		return m_root->HasObject( id );
	}

	interop::ObjectPtr EditableCollection::GetRootInterop()
	{
		return m_root;
	}

	void EditableCollection::RefreshView( Bool fullRefresh )
	{
		m_root->RefreshView( fullRefresh );
	}

} // tools