/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "textureArrayEditor.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../../common/redReflection/include/rttiPathParser.h"
#include "../../backendEngine/include/resourceBackend.h"
#include "../../backendEngine/include/editAction.h"
#include "textureArrayCompilationSource.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redReflection/include/util.h"
#include "textureArrayCooker.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureArrayResourceEditor, tools );
	RTTI_PARENT_TYPE( tools::IResourceEditor );
RTTI_END_TYPE();


namespace tools
{
	class ManipulateBitmapAction : public IEditAction
	{
	public:
		ManipulateBitmapAction( THandle< CTextureArray > texArray, const res::ResourcePath& bitmapPath, interop::InteropNotifier& notifier, Bool add )
			: m_texArray( texArray )
			, m_bitmapPath( bitmapPath )
			, m_notifier( notifier )
			, m_add( add )
		{
		}

		virtual String GetDescription() const override
		{
			return String::Printf( "Add bitmap %hs", m_bitmapPath.ToStringView() );
		}

		virtual Bool Do() override
		{
			if ( m_add )
			{
				return Add();
			}
			return Remove();
		}

		virtual Bool Undo() override
		{
			if ( m_add )
			{
				return Remove();
			}
			return Add();
		}

	private:
		Bool Add()
		{
			if ( m_bitmapPath.IsValid() )
			{
				TResRef< CBitmapTexture > textureRef( m_bitmapPath );
				if ( THandle< CBitmapTexture > textureHandle = textureRef.Get() )
				{
					for ( Uint32 i = 0; i < m_texArray->GetTextureCount(); ++i )
					{
						if ( m_texArray->GetTexture( i )->GetPath() == m_bitmapPath )
						{
							return false;
						}
					}
					
					if ( m_texArray->AddTexture( textureHandle ) )
					{
						m_notifier.Notify( String::Printf( "add:%hs", m_bitmapPath.ToString().AsChar() ) );
						return true;
					}
				}
			}
			return false;
		}

		Bool Remove()
		{
			if ( m_texArray->RemoveTexture( m_bitmapPath ) )
			{
				m_notifier.Notify( String::Printf( "remove:%hs", m_bitmapPath.ToString().AsChar() ) );
				return true;
			}
			return false;
		}

		THandle< CTextureArray > m_texArray;
		res::ResourcePath m_bitmapPath;
		interop::InteropNotifier& m_notifier;
		Bool m_add;
	};

	TextureArrayResourceEditor::TextureArrayResourceEditor()
	{
	}

	TextureArrayResourceEditor::~TextureArrayResourceEditor()
	{
		if ( m_textureArray )
		{
			m_textureArray = nullptr;
		}
	}

	Bool TextureArrayResourceEditor::Initialize( const THandle< CResource >& resource )
	{
		// get the texture array to edit
		m_textureArray = Cast< CTextureArray >( resource );
		if ( !m_textureArray )
			return false;

		return true;
	}

	const Bool TextureArrayResourceEditor::SupportsResourceClass( const rtti::ClassType* resourceClass ) const
	{
		return resourceClass->IsA( ClassID< CTextureArray >() );
	}

	const THandle< CResource > TextureArrayResourceEditor::GetResource() const
	{
		return Cast< CResource >( m_textureArray );
	}

	Bool TextureArrayResourceEditor::Compile()
	{
		tools::TextureArrayCooker cooker( { *m_textureArray } );
		if ( !cooker.DoCook() )
		{
			return false;
		}
		return true;
	}

	void TextureArrayResourceEditor::SetupInterop( TextureArrayResourceEditor* editor, ::interop::utils::Object& op )
	{
		THandle< CTextureArray > texArray = editor->m_textureArray;
		if ( texArray == nullptr )
		{
			return;
		}

		op.RegisterNotifier( "bitmapsChanged", editor->m_bitmapsCountChanged );

		op.RegisterFunctor<Bool, String>( "addBitmap",
			[editor, texArray]( ::interop::IContext& ctx, String bitmapDepotPath )
		{
			res::ResourcePath bitmapPath = res::ResourcePath::Build( bitmapDepotPath );
			if ( bitmapPath.IsValid() )
			{
				if ( !editor->GetAccessHistory().Execute( red::CreateUniquePtr< ManipulateBitmapAction >( texArray, bitmapPath, editor->m_bitmapsCountChanged, true ) ) )
				{
					ctx.ThrowException( "Failed to add texture." );
					return false;
				}
				return true;
			}
			else
			{
				ctx.ThrowException( "Invalid bitmap path." );
				return false;
			}
		} );

		op.RegisterFunctor<Bool, String>( "removeBitmap",
			[editor, texArray]( ::interop::IContext& ctx, String bitmapDepotPath )
		{
			res::ResourcePath bitmapPath = res::ResourcePath::Build( bitmapDepotPath );
			if ( bitmapPath.IsValid() )
			{
				if ( !editor->GetAccessHistory().Execute( red::CreateUniquePtr< ManipulateBitmapAction >( texArray, bitmapPath, editor->m_bitmapsCountChanged, false ) ) )
				{
					ctx.ThrowException( "Failed to remove texture." );
					return false;
				}
				return true;
			}
			else
			{
				ctx.ThrowException( "Invalid bitmap path." );
				return false;
			}
		} );

		op.RegisterFunctor<void>( "compile",
			[editor]( ::interop::IContext& ctx )
		{
			if ( !editor->Compile() )
			{
				ctx.ThrowException( "Texture compilation failed - error building render resource blob" );
			}
		} );
	}

} // interop