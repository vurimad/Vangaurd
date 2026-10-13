#pragma once

namespace tools
{
	class EditableCollection;

	class BACKEND_ENGINE_API ResolvedView
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ResolvedView( EditableCollection* collection );

		const CName GetViewClass() const { return m_viewClass; }
		EditableCollection* GetEditableCollection() const { return m_collection; }

		Bool IsReadOnly() const;
		Bool IsReadOnly( const rtti::AccessPath& path ) const;
		Bool IsEmpty() const;
		Bool IsMultiValue( const rtti::AccessPath& path ) const;
		Bool IsDefault( IRTTIContext& ctx, const rtti::AccessPath& accessPath, Uint8& outValue ) const;

		void SetEditableViews( const red::DynArray< red::SharedPtr< IEditableView > >& views );

		void GetRootProperties( rtti::EditableProperties& outRootProperties ) const;

		Bool GetValue( IRTTIContext& ctx, const rtti::AccessPath& accessPath, rtti::ValuePtr& outValue ) const;
		void GetCurrentValues( red::DynArray<rtti::ValuePtr>& outValues, const rtti::AccessPath& path = rtti::AccessPath() ) const;

		Bool ExecuteValueChange( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const red::DynArray< rtti::ValuePtr >& oldValues,
			const rtti::ValuePtr& newValue = rtti::ValuePtr() ) const;

		Bool ExecuteValueChange( IRTTIContext& ctx, const rtti::AccessPath& accessPath, const red::DynArray< rtti::ValuePtr >& oldValues,
			const red::DynArray< rtti::ValuePtr >& newValues ) const;

		Bool HasObject( const SerializableID& id ) const;

		const red::SharedPtr< IEditableView > GetNegotiatedView() const;
		const red::DynArray< red::SharedPtr< IEditableView > >& GetViews() const { return m_views; }

	private:
		red::DynArray< red::SharedPtr< IEditableView > > m_views; // views to edit
		CName m_viewClass; // negotiated view class

		EditableCollection* m_collection; // Collection
	};
}