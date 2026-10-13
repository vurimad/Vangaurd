/**
* Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace red
{
	class TagList;
}

namespace tools
{
	class IResourceEditor;
	class IIncludedTagsProvider;

	class BACKEND_ENGINE_API IIncludedTagsProvider
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IIncludedTagsProvider() {}
		virtual red::TagList GetIncludedTags( const rtti::AccessPath& path ) const = 0;
	};

	class IConvertProvider
	{
	public:
		virtual ~IConvertProvider() {}
		virtual void Convert( String& inOutTxt ) const = 0;
	};

	class BACKEND_ENGINE_API ResourceEditorUserContext : public ISerializable
	{
		RTTI_DECLARE_TYPE( ResourceEditorUserContext );
		RED_BASE_CLASS( ISerializable )

	public:
		ResourceEditorUserContext();
		ResourceEditorUserContext( IResourceEditor* editor );
		virtual ~ResourceEditorUserContext() override;

		template< class EditorType >
		EditorType* GetEditor()
		{
			static_assert( std::is_base_of<IResourceEditor, EditorType>::value, "Must be editor type" );
			return static_cast< EditorType* >( m_editor );
		}

		template< class EditorType >
		const EditorType* GetEditor() const
		{
			static_assert( std::is_base_of<IResourceEditor, EditorType>::value, "Must be editor type" );
			return static_cast<const EditorType*>( m_editor );
		}

		IIncludedTagsProvider* GetIncludedTagsProvider() { return m_includedTagsProvider.Get(); }
		void SetIncludedTagsProvider( IIncludedTagsProvider* provider );

		IConvertProvider* GetConvertProvider() { return m_convertProvider.Get(); }
		void SetConvertProvider( IConvertProvider* convertProvider );

	private:
		IResourceEditor* m_editor;

		red::UniquePtr< IIncludedTagsProvider > m_includedTagsProvider;
		red::UniquePtr< IConvertProvider > m_convertProvider;
	};
}