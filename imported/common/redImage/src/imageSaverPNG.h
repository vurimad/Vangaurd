/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "imageSaver.h"

namespace img
{

	namespace prv
	{

		/// Image saver for PNG files
		class ImageSaverPNG : public BaseImageSaver
		{
		public:
			ImageSaverPNG();
			virtual Bool DoSave( IFile& writer, const Image& image, const AnsiChar* typeHint ) const override final;
		};

	} // prv

} // img