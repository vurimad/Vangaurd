/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "imageLoader.h"

namespace img
{

	namespace prv
	{

		/// Image loader for PNG files
		class ImageLoaderPNG : public BaseImageLoader
		{
		public:
			ImageLoaderPNG();
			virtual ImagePtr DoLoad( IFile& reader, const AnsiChar* typeHint ) const override final;
		};

	} // prv

} // img