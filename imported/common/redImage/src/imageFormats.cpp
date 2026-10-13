/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "imageLoaderPNG.h"
#include "imageSaverPNG.h"

namespace img
{
	static Bool			initialized = false;
	static red::Mutex	initializerLock;

	void RegisterFormats()
	{
		if ( !initialized )
		{
			RED_SCOPE_LOCK( initializerLock );

			if( !initialized )
			{
				// PNG
				BaseImageLoader::RegisterLoader( RED_NEW( prv::ImageLoaderPNG )() );
				BaseImageSaver::RegisterSaver( RED_NEW( prv::ImageSaverPNG )() );

				// JPG
				//BaseImageLoader::RegisterLoader( RED_NEW( prv::ImageLoaderJPG )() );
				//BaseImageSaver::RegisterSaver( RED_NEW( prv::ImageSaverJPG )() );

				// TGA
				//BaseImageLoader::RegisterLoader( RED_NEW( prv::ImageLoaderTGA )() );
				//BaseImageSaver::RegisterSaver( RED_NEW( prv::ImageSaverTGA )() );
			}

			RED_THREADS_MEMORY_BARRIER();

			// make sure we do it only once
			initialized = true;
		}
	}

} // img
