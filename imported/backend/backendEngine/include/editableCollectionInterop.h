/**
* Copyright (c) 2015-2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableFwd.h"
#include "editableCollectionCustomPropertyEditor.h"

#include "../../../common/interop/include/interopBase.h"

#include "../../../common/redReflection/include/serializableValueNotifier.h"
#include "../../../common/generalEditorBackend/include/interopControlProxy.h"


namespace rtti
{
	class IType;
}

namespace tools
{
	class ResolvedView;
	class DataControlInterop;

	namespace prv
	{
		class EditableCollectionRootItem;
		class EditableCollectionArrayItem;
		class EditableCollectionStructItem;
		class EditableCollectionPropertyItem;
		class EditableCollectionInlinedRootItem;
		class EditableCollectionDataControlBackend;

		struct EditableCollectionContext
		{
			EditableCollection* collection; 
			EditableCollectionRootItem* root;
			rtti::AccessPath path; 
			rtti::ClassEditablePropertyInfo propertyInfo;
		};

		class BACKEND_ENGINE_API InteropRTTIContextAdapter : public IRTTIContext
		{
		public:
			InteropRTTIContextAdapter( interop::IContext& ctx );
			virtual ~InteropRTTIContextAdapter();

			virtual const Bool ReportError( const rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const AnsiChar* txt, ... ) override final;

		private:
			interop::IContext*	m_context;
		};

		///---

		// Wrapper for custom property control
		// TODO: this can be probably merged with EditableCollectionPropertyItem
		class BACKEND_ENGINE_API EditableCollectionCustomPropertyItem : public interop::IValueObject
		{
		public:
			EditableCollectionCustomPropertyItem( red::SharedPtr<IEditableCustomProperty> control, const rtti::AccessPath& pathSoFar );
			virtual ~EditableCollectionCustomPropertyItem() override;

		private:
			// Interop implementation
			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override final;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override final;
			
			virtual const Bool IsReadOnly() const override final;
			virtual const Uint8 IsDefault() const override final;
			virtual const Bool IsMultiValue() const override final;

			virtual const CName GetType() const override final;
			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;
			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override final;
			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

			red::SharedPtr<IEditableCustomProperty> m_control;
			rtti::AccessPath						m_path;		// Rtti Path
		};

		///---

		/// Editable property definition provides a way to explore the property layout
		/// Generalization of the const rtti::Property for editor purposes
		/// Hoarded in the editable collections
		class BACKEND_ENGINE_API EditableCollectionPropertyItem : public interop::IValueObject
		{
		public:
			EditableCollectionPropertyItem( const EditableCollectionContext & context );
			virtual ~EditableCollectionPropertyItem() override;

		protected:
			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;
			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

		private:
			const rtti::IType*			m_type;					// Type of the property
			CName						m_typeName;				// Editor name of the type

			Uint8						m_hasMultipleValues:1;	// Property has multiple values
			Uint8						m_isReadOnly:1;			// Property is read only

			EditableCollection*			m_collection;			// Collection
			EditableCollectionRootItem* m_root;					// Data root

			rtti::AccessPath			m_path;					// Rtti Path
			Bool						m_canReset;
			String						m_tooltip;

			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override final;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override final;
			
			virtual const Bool IsReadOnly() const override final;
			virtual const Uint8 IsDefault() const override final;
			virtual const Bool IsMultiValue() const override final;
			virtual const CName GetType() const override final;

			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override final;

			virtual void FormatReturnValue( const rtti::ValuePtr& value, interop::InteropString& outValue ) const;
			virtual rtti::ValuePtr FormatInputValue( const interop::InteropString& value ) const;

			friend class EditableCollectionInlinedRootItem;
		};

		///---

		class BACKEND_ENGINE_API EditableCollectionEnumPropertyItem : public EditableCollectionPropertyItem
		{
		public:
			EditableCollectionEnumPropertyItem( const EditableCollectionContext & context );

		private:
			const rtti::EnumType*	m_enumType;

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;
		};

		///---

		class BACKEND_ENGINE_API EditableCollectionBitfieldPropertyItem : public EditableCollectionPropertyItem
		{
		public:
			
			EditableCollectionBitfieldPropertyItem( const EditableCollectionContext & context  );

		private:
			const rtti::BitFieldType* m_bitfieldType;

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;

			virtual void FormatReturnValue( const rtti::ValuePtr& value, interop::InteropString& outValue ) const;
			virtual rtti::ValuePtr FormatInputValue( const interop::InteropString& value ) const;
		};

		///---

		class BACKEND_ENGINE_API EditableCollectionRangedPropertyItem : public EditableCollectionPropertyItem
		{
		public:
			EditableCollectionRangedPropertyItem(  const EditableCollectionContext & context );

		private:
			Float	m_minValue;
			Float	m_maxValue;
			Float	m_xMinValue;
			Float	m_xMaxValue;
			Bool	m_useSlider;

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;
		};

		///---

		class BACKEND_ENGINE_API EditableCollectionArrayItem : public interop::IArrayObject
		{
		public:
			EditableCollectionArrayItem( const EditableCollectionContext & context );
			virtual ~EditableCollectionArrayItem() override;

			EditableCollectionRootItem* GetRoot() const { return m_root; }
			const rtti::IBaseArrayType* GetArrayType() const { return m_arrayType; }

			// functions
			const Bool ArrayClear( interop::IContext& ctx );
			const Bool ArrayAddElement( interop::IContext& ctx );
			const Bool ArrayInsertElement( interop::IContext& ctx, const Uint32 index );
			const Bool ArrayRemoveElement( interop::IContext& ctx, const Uint32 index );
			const Bool ArrayMoveElement( interop::IContext& ctx, const Uint32 from, const Uint32 to );
			const Bool ArraySwapElements( interop::IContext& ctx, const Uint32 from, const Uint32 to );

			void CreateItem( const Uint32 index );
			void RemoveItem( const Uint32 index );
			void RebindRange( const Uint32 from, const Uint32 to, const Int32 indexOffset );

			virtual const Uint32 GetSize( interop::IContext& ctx ) const override final;

			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

		private:
			virtual const interop::ObjectPtr GetElement( interop::IContext& ctx, const Uint32 index ) const override final;
			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;
			virtual const CName GetType() const override final;
			virtual const Bool IsReadOnly() const override final;
			virtual const Bool IsMultiValue() const override final;
			virtual const Bool IsResizable() const override final;
			virtual const Uint32 GetMinSize() const override final;
			virtual const Uint32 GetMaxSize() const override final;
			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) override;

			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override final;

			void RebuildAll();
			void HandleDirectChange( const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue );

			EditableCollection* m_collection;			// Collection
			EditableCollectionRootItem* m_root;			// Parent views

			rtti::AccessPath m_path;					// path so far
			
			const rtti::IBaseArrayType*	m_arrayType;
			CName m_typeName;

			CName m_innerTypeCustomTypeName;

			red::DynArray< interop::ObjectPtr > m_elements;

			Bool m_isReadOnly;
			Bool m_isInlined;
			Bool m_isResizable;
			Bool m_canAdd;
			Bool m_canClear;
			Bool m_canSelect;
			Bool m_canReset;	// Property affects to both, array and elements
			Uint32 m_minSize;
			Uint32 m_maxSize;
			Float m_minValue;
			Float m_maxValue;
			String m_tooltip;
			String m_keyPath;
		};

		///---

		/// Structure (does not own separate memory)
		class BACKEND_ENGINE_API EditableCollectionStructItem : public interop::ICompoundObject
		{

		public:			
			EditableCollectionStructItem( const EditableCollectionContext & context );
			virtual ~EditableCollectionStructItem() override;

		protected:
			virtual interop::ObjectPtr GetChild( interop::IContext& ctx, CName name ) const override;
			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;

			const rtti::AccessPath& GetPath() const { return m_path; }
			virtual void GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const override;

			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

		private:
			virtual const Bool IsMultiValue() const override;
			virtual const Uint8 IsDefault() const override;
			virtual const Bool IsReadOnly() const override;
			virtual const CName GetType() const override;

			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override final;

			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override;

			void CopyCategory( interop::IContext& ctx, const CName& categoryName ) const;
			void PasteCategory( interop::IContext& ctx, const CName& categoryName ) const;

			EditableCollection*			m_collection;			// Collection
			EditableCollectionRootItem* m_parentRoot;			// Previous (parent) root - may be NULL
			CName						m_customTypeName;		// Editor name of the type

			rtti::AccessPath			m_path;					// path so far

			const rtti::ClassType*		m_structType;
			Bool						m_isReadOnly;
			Bool						m_canReset;
			String						m_tooltip;
			Float						m_minValue;
			Float						m_maxValue;

			struct Child
			{
				RED_USE_MEMORY_POOL( red::PoolBackend );

				CName					m_name;
				CName					m_category;
				interop::ObjectPtr		m_object;
			};

			red::DynArray< Child >		m_children;
		};

		class BACKEND_ENGINE_API EditableCollectionCustomStructItem : public EditableCollectionStructItem
		{
		public:
			EditableCollectionCustomStructItem( red::SharedPtr<IEditableCustomProperty> control, const EditableCollectionContext & context );

		private:
			virtual interop::ObjectPtr GetChild( interop::IContext& ctx, CName name ) const override;
			virtual void GetChildrenInfo(interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo) const override final;

			virtual const Bool IsMultiValue() const override;
			virtual const Bool IsReadOnly() const override;
			virtual const CName GetType() const override;

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;

			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override;

			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

			red::SharedPtr<IEditableCustomProperty> m_control;
		};

		/// Editable root - represents a group o IEditableViews
		class BACKEND_ENGINE_API EditableCollectionRootItem : public interop::ICompoundObject
		{
		public:
			EditableCollectionRootItem( EditableCollection* collection, EditableCollectionRootItem* parentRoot );
			virtual ~EditableCollectionRootItem();

			// Set single editable view
			void SetEditableView( red::SharedPtr< IEditableView >& view );

			// Set new list of views - may recreate all internal children (or not)
			void SetEditableViews( const red::DynArray< red::SharedPtr< IEditableView > >& views );

			// read value - will read it from all views
			const Bool ReadValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, rtti::ValuePtr& outValue ) const;

			// write new value, will write it to all views using the action executor from the collection
			const Bool WriteValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const rtti::ValuePtr& newValue,
				const red::DynArray< rtti::ValuePtr >& oldValues = red::DynArray< rtti::ValuePtr >() ) const;
			
			// write new values, will write different values to corresponding views using the action executor from the collection
			const Bool WriteValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const red::DynArray< rtti::ValuePtr >& newValues,
				const red::DynArray< rtti::ValuePtr >& oldValues = red::DynArray< rtti::ValuePtr >() ) const;

			// reset given property in all views
			const Bool ResetValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath,
				const red::DynArray< rtti::ValuePtr >& oldValues = red::DynArray< rtti::ValuePtr >() ) const;

			// is default value; returns false if not supported
			const Bool IsDefault( IRTTIContext& ctx, const rtti::AccessPath& accessPath, Uint8& outValue ) const;

			// returns true if a view of object with given id is in the views list
			const Bool HasObject( const SerializableID& id ) const;

			// recreates internal children
			void RefreshView( Bool fullRefresh );

			// ICompoundObject function override
			virtual const Bool IsReadOnly() const override final;
			const Bool IsMultiValue( const rtti::AccessPath& propertyPath ) const;

			// IBaseObject function override
			virtual const CName GetType() const override final;
			const CName GetViewClass() const;

			// Get negotiated view (be careful with this, always check if you really can use the negotiated view to get additional information)
			// It can give you access to one ISerializable, so you can check some common / shared data for all of the views if you're sure it's really shared.
			const red::SharedPtr< IEditableView > GetNegotiatedView() const;

			const red::SharedPtr< ResolvedView >& GetResolvedView() const { return m_resolvedView; }

			void RegisterItem( interop::IBaseObject* item );
			void UnregisterItem( interop::IBaseObject* item );

			interop::IBaseObject* GetItemById( const proxy::GenericId id );
			proxy::GenericId GetItemId( const interop::IBaseObject* ) const;

			void NotifyItemCreated( interop::IBaseObject* parent, interop::IBaseObject* item, const Uint32 index );
			void NotifyItemRemoved( interop::IBaseObject* item );
			void NotifyItemMoved( interop::IBaseObject* item, const Uint32 index );

			void ProcessNotification( const interop::IBaseObject* object ) const;

		private:
			red::SharedPtr< EditableCollectionDataControlBackend > m_dataControlBackend;
			red::UniquePtr< DataControlInterop > m_dataControlInterop;

		protected:
			virtual void GetChildrenInfo( interop::IContext& ctx, red::DynArray< interop::ChildWithCategory >& outInfo ) const override final;
			virtual interop::ObjectPtr GetChild( interop::IContext& ctx, CName name ) const override final;
			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;

			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override final;

			virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue );

			red::SharedPtr< EditableCollectionDataControlBackend > GetDataControlBackend();

			EditableCollectionRootItem* m_parentRoot; // Previous (parent) root - may be NULL, not NULL for inlined objects

			red::SharedPtr< ResolvedView > m_resolvedView;

			CName m_propertyType; // type reported to the owner

			struct Child
			{
				RED_USE_MEMORY_POOL( red::PoolBackend );

				CName m_name;
				CName m_category;
				interop::ObjectPtr m_object;
			};

			red::DynArray< Child > m_children;

		private:
			void CopyCategory( interop::IContext& ctx, const CName& categoryName ) const;
			void PasteCategory( interop::IContext& ctx, const CName& categoryName ) const;

			SerializableValueNotifier m_serializableNotifier;
		};

		/// inlined root
		class BACKEND_ENGINE_API EditableCollectionInlinedRootItem : public EditableCollectionRootItem
		{
		public:
			EditableCollectionInlinedRootItem( const EditableCollectionContext & context );

			const Bool RebuildViews();
			void ResetValue( interop::IContext& ctx ) const;

		protected:
			virtual void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) override;

		private:
			virtual void Get( interop::IContext& ctx, interop::InteropString& outValue ) const override final;
			virtual void Set( interop::IContext& ctx, const interop::InteropString& newValue ) const override final;

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override final;

			virtual const Bool IsMultiValue() const override final;

			virtual void Rebind( const rtti::AccessPath& oldPath, const rtti::AccessPath& newPath ) override;

			rtti::AccessPath	m_path;
			Bool m_isReadOnly;	// TODO: support all three cases: 1) property is RO but it's content not, 2) property is not RO, but the content is, 3) both are RO
			Bool m_canClear;
			Bool m_canReset;
			Bool m_canSelect;
			String m_tooltip;
			String m_keyPath;
		};

		typedef std::function< void( interop::IContext& ctx ) > TReset;

		template< typename T >
		class ResetableValueObject : public interop::utils::ValueObject<T>
		{
		public:
			ResetableValueObject( EditableCollectionRootItem* parent, TReset resetFunc, TGetter getter, TSetter setter, interop::InteropNotifier* optionalNotifier = nullptr )
				: interop::utils::ValueObject<T>( getter, setter, optionalNotifier )
				, m_root( parent )
				, m_resetFunction( resetFunc )
			{}

			ResetableValueObject( EditableCollectionRootItem* parent, TReset resetFunc, TGetter getter, interop::InteropNotifier* optionalNotifier = nullptr )
				: interop::utils::ValueObject<T>( getter, optionalNotifier )
				, m_root( parent )
				, m_resetFunction( resetFunc )
			{}

			virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const
			{
				if ( name == "_reset" )
				{
					return interop::ObjectPtr( RED_NEW( interop::utils::Functor0< void > )( m_resetFunction ) );
				}

				return interop::utils::ValueObject<T>::GetUnlistedChild( ctx, name );
			}

			virtual const Bool ProcessNotification( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue ) const override
			{
				m_root->ProcessNotification( this );
				return true;
			}


		private:
			EditableCollectionRootItem* m_root;
			TReset m_resetFunction;
		};

		class BACKEND_ENGINE_API ResetFunctor : public interop::IFunctorObject
		{
		public:
			ResetFunctor( const EditableCollectionRootItem* parent, const rtti::AccessPath& path );

			virtual void GetParamTypes( interop::IContext&, red::DynArray< CName >& ) const;

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const;

			virtual const CName GetType() const override final;

		private:
			const EditableCollectionRootItem* m_root;
			rtti::AccessPath m_path;
		};

	} // prv

} // tools