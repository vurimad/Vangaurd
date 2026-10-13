/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{
	/// Image loading handler +abstract
	class REDIMAGE_API BaseImageLoader
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		BaseImageLoader();
		virtual ~BaseImageLoader();

		/// Register loader format into the global registry
		/// NOTE: DO NOT destroy the object 
		static void RegisterLoader( BaseImageLoader* loader );

		/// Load image from the data stream
		/// NOTE: Make sure you called img::RegisterFormats() before calling this function ;]
		/// NOTE: the type hint can be passed for the undecided readers to narrow the possibilities (usually, the typeHint is the extension of the source file)
		static ImagePtr Load( IFile& reader, const AnsiChar* typeHint = "" );

	protected:
		/// Try to load image from reader stream, can fail if image type is not supported
		virtual ImagePtr DoLoad( IFile& reader, const AnsiChar* typeHint ) const = 0;
	};

} // img