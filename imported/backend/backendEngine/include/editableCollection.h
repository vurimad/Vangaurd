/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableFwd.h"
#include "editableCollectionCustomPropertyEditor.h"
#include "editableCollectionInterop.h"

#include "../../../common/interop/include/interopBase.h"

namespace tools
{

	namespace prv
	{
		class EditableCollectionArrayItem;
		class EditableCollectionStructItem;
		class EditableCollectionPropertyItem;
		class EditableCollectionRootItem;
	}

	///-----

	/// group of editable views than can be edited together
	class BACKEND_ENGINE_API EditableCollection
	{
	public:
		EditableCollection();
		~EditableCollection();

		typedef red::DynArray< red::SharedPtr< IEditableView > > TViews;

		struct ExecuteData
		{
			TViews								m_views;			// views to set the data on
			rtti::AccessPath					m_path;				// path to the internal value to change
			rtti::ValuePtr						m_newValue;			// value to set
			red::DynArray< rtti::ValuePtr >		m_oldValues;		// old values
		};

		/// bind the "Execute" function - without it the created collection is always only
		typedef std::function< Bool(const ExecuteData& data) > TExecutor;
		void BindExecutor( const TExecutor& executor );

		typedef std::function< void(Bool) > TTransactionHandler;
		void BindTransactionHandler( const TTransactionHandler& transactionHandler );

		/// set new value, uses the bounded executor, provides notification
		const Bool ExecuteValueChange( const ExecuteData& data ) const;

		void BeginTransaction();
		void EndTransaction();

		/// clear - remove all objects
		void Clear();

		/// set single root view
		void SetObject( const red::SharedPtr< IEditableView >& view );

		/// set single proxy, the widest view will be negotiated
		void SetObject( const red::SharedPtr< IEditableProxy >& proxy );

		/// set single root - shortcut for simple interface, the widest view will be negotiated
		void SetObject( const THandle< ISerializable >& object );

		/// set multiple objects
		void SetObjects( const red::DynArray< red::SharedPtr< IEditableView > >& views );

		/// returns true if current view concerns object with given id
		Bool HasObject( const SerializableID& id ) const;

		/// get interop interface (it's cached - we can do it here)
		interop::ObjectPtr GetRootInterop();

		/// refresh current editable view
		void RefreshView( Bool fullRefresh );

	public:
		// interop executor
		TExecutor m_executor;
		TTransactionHandler m_transactionHandler;

		// property structure
		red::SharedPtr< prv::EditableCollectionRootItem > m_root;
	};

} // tools
