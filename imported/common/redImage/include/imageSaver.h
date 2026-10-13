/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{
	/// Image saving handler
	class REDIMAGE_API BaseImageSaver
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		BaseImageSaver();
		virtual ~BaseImageSaver();

		/// Register saver format into the global registry
		/// NOTE: DO NOT destroy the object 
		static void RegisterSaver( BaseImageSaver* saver );

		// Save image to file (may be in memory)
		/// NOTE: Make sure you called img::RegisterFormats() before calling this function ;]
		/// NOTE: the type hint can be passed for the undecided readers to narrow the possibilities (usually, the typeHint is the extension of the source file)
		static Bool Save( IFile& writer, const ImagePtr& image, const AnsiChar* typeHint = "" );

	protected:
		/// Try to save image, can fail if format is not supported
		virtual Bool DoSave( IFile& writer, const Image& image, const AnsiChar* typeHint ) const = 0;
	};

} // img