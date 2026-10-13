/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"
#include "imageLoader.h"
#include "../../../common/redFileSystem/include/file.h"

namespace img
{

	namespace prv
	{
		class LoaderList
		{
		public:
			RED_FORCE_INLINE LoaderList() = default;

			RED_FORCE_INLINE static LoaderList& GetInstance()
			{
				static LoaderList theInstance;
				return theInstance;
			}

			RED_FORCE_INLINE void Add( BaseImageLoader* loader )
			{
				m_loaders.PushBack( loader );
			}

			typedef red::DynArray< BaseImageLoader* > TFormats;
			RED_FORCE_INLINE const TFormats& List() const
			{
				return m_loaders;
			}

		private:
			TFormats m_loaders{ red::PoolEngine() };
		};
	}

	BaseImageLoader::BaseImageLoader() = default;

	BaseImageLoader::~BaseImageLoader() = default;

	void BaseImageLoader::RegisterLoader( BaseImageLoader* loader )
	{
		prv::LoaderList::GetInstance().Add( loader );
	}

	ImagePtr BaseImageLoader::Load( IFile& reader, const AnsiChar* typeHint/* = ""*/ )
	{
		// get current reading position
		const auto baseOffset = reader.GetOffset();

		img::RegisterFormats();

		// try to load file
		for ( const auto* loader : prv::LoaderList::GetInstance().List() )
		{
			// reset file to the initial state
			reader.Seek( baseOffset );

			// try to load the image
			auto loadedImage = loader->DoLoad( reader, typeHint );
			if ( loadedImage )
				return loadedImage;
		}

		// nothing loaded
		return ImagePtr();
	}

} // img