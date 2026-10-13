/**
* Copyright (c) 2017-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace tools
{
	namespace prv
	{
		class EditableCollectionRootItem;

		class PathAndValue : public ISerializable
		{
			RTTI_DECLARE_TYPE( PathAndValue )

		public:
			PathAndValue() = default;
			PathAndValue( const rtti::AccessPath& path );

			String m_path;
			String m_value;
		};

		class BaseClipboardHolder : public ISerializable
		{
			RTTI_DECLARE_TYPE( BaseClipboardHolder )

		public:
			BaseClipboardHolder() = default;

			Uint32 AddExport( const THandle< ISerializable >& object );
			THandle< ISerializable > GetExport( Uint32 index ) const;

			Bool CopyValue( const EditableCollectionRootItem* rootItem, const rtti::AccessPath& path, String& outValue );
			Bool PasteValue( EditableCollectionRootItem* rootItem, const rtti::AccessPath& path, const String& value );

			virtual Bool Copy( interop::IContext& ctx, const EditableCollectionRootItem* rootItem ) = 0;
			virtual Bool Paste( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths ) = 0;

		private:
			rtti::ValuePtr ExtractExports( rtti::ValuePtr& value );
			rtti::ValuePtr SupplementExports( rtti::ValuePtr value );

			red::DynArray< THandle< ISerializable > > m_exports;
		};

		class BACKEND_ENGINE_API SingleElementClipboardHolder : public BaseClipboardHolder
		{
			RTTI_DECLARE_TYPE( SingleElementClipboardHolder )

		public:
			SingleElementClipboardHolder() = default;
			SingleElementClipboardHolder( const rtti::AccessPath& path );
			SingleElementClipboardHolder( const rtti::AccessPath& path, const String& value );

			virtual Bool Copy( interop::IContext& ctx, const EditableCollectionRootItem* rootItem ) override;
			virtual Bool Paste( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths ) override;

		private:
			rtti::AccessPath m_path;
			String m_value;
		};

		class CategoryClipboardHolder : public BaseClipboardHolder
		{
			RTTI_DECLARE_TYPE( CategoryClipboardHolder )

		public:
			CategoryClipboardHolder() = default;
			CategoryClipboardHolder( const CName categoryName, const red::DynArray< rtti::AccessPath >& pathsToCopy );

			const Bool GetEntry( const rtti::AccessPath& accessPath, String& outValue ) const;

			virtual Bool Copy( interop::IContext& ctx, const EditableCollectionRootItem* rootItem ) override;
			virtual Bool Paste( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths ) override;

		private:
			CName m_categoryName;
			red::DynArray< PathAndValue > m_pathsAndValues;
		};

		class BACKEND_ENGINE_API CopyPasteBaseFunctor : public interop::IFunctorObject
		{
		public:
			CopyPasteBaseFunctor( const rtti::AccessPath& path );

			virtual const CName GetType() const override { return CName::NONE(); }
			virtual void GetParamTypes( interop::IContext& ctx, red::DynArray< CName >& outTypes ) const override {}

		protected:
			const rtti::AccessPath m_accessPath;
		};

		//////////////////////////////////////////////////////////////////////////

		class BACKEND_ENGINE_API CopyPropertyItemFunctor : public CopyPasteBaseFunctor
		{
		public:
			CopyPropertyItemFunctor( const EditableCollectionRootItem* item, const rtti::AccessPath& accessPath );

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const override;
			virtual Bool IsConst() const override { return true; }

		private:
			const EditableCollectionRootItem* m_root;
		};

		class BACKEND_ENGINE_API PastePropertyItemFunctor : public CopyPasteBaseFunctor
		{
		public:
			PastePropertyItemFunctor( EditableCollectionRootItem* item, const rtti::AccessPath& accessPath );

			virtual interop::InteropString Execute( interop::IContext& ctx, const Uint32 numArgs, const interop::InteropString* args ) const override;

		private:
			EditableCollectionRootItem* m_root;
		};

		extern void CopyProperties( interop::IContext& ctx, const EditableCollectionRootItem* rootItem, const THandle< BaseClipboardHolder >& holder );
		extern void PasteProperties( interop::IContext& ctx, EditableCollectionRootItem* rootItem, const red::DynArray< rtti::AccessPath >& paths );
	}
}