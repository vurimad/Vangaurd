/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"
#include "imageSaver.h"
#include "../../../common/redFileSystem/include/file.h"

namespace img
{

	namespace prv
	{
		class SaverList
		{
		public:
			RED_FORCE_INLINE SaverList()
			{
			}

			RED_FORCE_INLINE static SaverList& GetInstance()
			{
				static SaverList theInstance;
				return theInstance;
			}

			RED_FORCE_INLINE void Add( BaseImageSaver* loader )
			{
				m_loaders.PushBack( loader );
			}

			typedef red::DynArray< BaseImageSaver* > TFormats;
			RED_FORCE_INLINE const TFormats& List() const
			{
				return m_loaders;
			}

		private:
			TFormats	m_loaders{ red::PoolEngine() };
		};
	}

	BaseImageSaver::BaseImageSaver()
	{
	}

	BaseImageSaver::~BaseImageSaver()
	{
	}

	void BaseImageSaver::RegisterSaver( BaseImageSaver* saver )
	{
		prv::SaverList::GetInstance().Add( saver );
	}

	Bool BaseImageSaver::Save( IFile& writer, const ImagePtr& image, const AnsiChar* typeHint/* = ""*/ )
	{
		// no image to save
		if ( !image )
			return false;

		// get current reading position
		const auto baseOffset = writer.GetOffset();

		img::RegisterFormats();

		// try to load file
		for ( const auto* saver : prv::SaverList::GetInstance().List() )
		{
			// reset file to the initial state
			writer.Seek( baseOffset );

			// try to load the image
			if ( saver->DoSave( writer, *image, typeHint ) )
				return true;
		}

		// not saved
		return false;
	}

} // img