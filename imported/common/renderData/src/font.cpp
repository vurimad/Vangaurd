//
// Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
//	

#include "build.h"
#include "font.h"
#include "fontStyle.h"

#if !defined( RED_PLATFORM_LINUX )
#	define RED_USE_FREE_TYPE
#endif

#ifdef RED_USE_FREE_TYPE
#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_BITMAP_H
#include FT_ADVANCES_H
#include FT_STROKER_H
#endif // RED_USE_FREE_TYPE

#include "../../redSystem/include/stopWatch.h"


RTTI_BEGIN_TYPE_IN_NAMESPACE( Font, rend );
	RTTI_PARENT_TYPE( CResource );
	RTTI_PROPERTY( m_fontBuffer );
RTTI_END_TYPE();

namespace rend {

namespace {

	static void* FreeTypeAlloc( FT_Memory memory, long size )
	{
		return RED_ALLOCATE( PoolFreeType, size );
	}

	static void* FreeTypeRealloc( FT_Memory memory, long curSize, long newSize, void* block )
	{
		return RED_REALLOCATE( PoolFreeType, block, newSize );
	}

	static void FreeTypeFree( FT_Memory memory, void* block )
	{
		return RED_FREE( PoolFreeType, block );
	}

} // namespace

// additional margin around glyphs (in pixels) to avoid sharp edges when upsampling
static const Uint32 c_textMarigin = 2;

FreeTypeLibraryWrapper::FreeTypeLibraryWrapper()
	: m_memory( nullptr )
	, m_library( nullptr )
{
#ifdef RED_USE_FREE_TYPE
	m_memory = RED_NEW( FT_MemoryRec_, PoolFont );
	RED_FATAL_ASSERT( m_memory );

	// Init FreeType
	m_memory->alloc = &FreeTypeAlloc;
	m_memory->realloc = &FreeTypeRealloc;
	m_memory->free = &FreeTypeFree;
	m_memory->user = this;

	FT_Error error = FT_New_Library( m_memory, &m_library );
	if ( error )
	{
		RED_LOG_ERROR( "Unable to init FreeType library, error code: %u", error );
		return;
	}

	FT_Add_Default_Modules( m_library );

	FT_Int major, minor, patch;
	FT_Library_Version( m_library, &major, &minor, &patch );
	RED_LOG_INFO( "FreeType library initialized, version %d.%d.%d", major, minor, patch );
#endif // RED_USE_FREE_TYPE
}

FreeTypeLibraryWrapper::~FreeTypeLibraryWrapper()
{
#ifdef RED_USE_FREE_TYPE
	if ( m_library )
	{
		FT_Done_Library( m_library );
		m_library = nullptr;
	}
#endif // RED_USE_FREE_TYPE
}

FreeTypeLibraryWrapper& FreeTypeLibraryWrapper::GetInstance()
{
	static FreeTypeLibraryWrapper s_instance;
	return s_instance;
}

Font::Font()
	: m_face( nullptr )
	, m_cache{ PoolFont() }
{
}

Font::~Font()
{
	Release();
}

const char* Font::GetFontName() const
{
#ifdef RED_USE_FREE_TYPE
	if ( m_face && m_face->family_name )
	{
		return m_face->family_name;
	}
#endif
	return "<none>";
}

const char* Font::GetStyleName() const
{
#ifdef RED_USE_FREE_TYPE
	if ( m_face && m_face->style_name )
	{
		return m_face->style_name;
	}
#endif
	return "<none>";
}

bool Font::SetFontBuffer( red::DynArray< Uint8 >&& buffer )
{
	RED_FATAL_ASSERT( !m_face, "Cannot set new font buffer while the font is being used" );

	m_fontBuffer = DataBuffer::Copy( buffer.Data(), buffer.Size() );

	bool canInitialize = Initialize();
	Release();
	return canInitialize;
}

bool Font::Initialize()
{
	Release();

#ifdef RED_USE_FREE_TYPE

	FT_Library library = FreeTypeLibraryWrapper::GetInstance().GetLibrary();

	FT_Error error;

	// Note: FT_New_Face/FT_Done_Face are not thread-safe
	FreeTypeLibraryWrapper::GetInstance().GetMutex().Acquire();
	error = FT_New_Memory_Face( library, (const FT_Byte*)m_fontBuffer.Data(), m_fontBuffer.Size(), 0, &m_face );
	FreeTypeLibraryWrapper::GetInstance().GetMutex().Release();

	if ( error )
	{
		RED_LOG_ERROR( "Font: Unable to retrieve font face! Resource path: %hs", GetPath().ToDebugString() );
		return false;
	}

	error = FT_Select_Charmap( m_face, FT_ENCODING_UNICODE );
	if ( error )
	{
		RED_LOG_ERROR( "Font: Unable to select unicode charmap! Resource path: %hs", GetPath().ToDebugString() );
		return false;
	}

#endif // RED_USE_FREE_TYPE

	return true;
}

void Font::Release()
{
#ifdef RED_USE_FREE_TYPE
	if ( m_face )
	{
		// Note: FT_New_Face/FT_Done_Face are not thread-safe
		FreeTypeLibraryWrapper::GetInstance().GetMutex().Acquire();
		FT_Done_Face( m_face );
		FreeTypeLibraryWrapper::GetInstance().GetMutex().Release();

		m_face = nullptr;
	}
#endif // RED_USE_FREE_TYPE
}

void Font::OnPostLoad( const PostLoadContext& context )
{
	Initialize();
}

GlyphIndex Font::GetGlyphIndexForChar( red::CodePoint codePoint ) const
{
#ifdef RED_USE_FREE_TYPE
	return FT_Get_Char_Index( m_face, codePoint );
#else
	return 0;
#endif // RED_USE_FREE_TYPE
}

const Font::Metrics Font::GetMetrics( const FontStyle& style ) const
{
	Metrics metrics;

#ifdef RED_USE_FREE_TYPE
	if ( m_face )
	{
		const Float scale = 1.0f / m_face->units_per_EM;
		metrics.ascender = static_cast<Int32>( m_face->ascender * (Float)style.size * scale );
		metrics.descender = static_cast<Int32>( m_face->descender * (Float)style.size * scale );
		metrics.lineSpacing = static_cast<Int32>( m_face->height * (Float)style.size * scale );
		metrics.maxGlyphWidth = static_cast<Int32>( ( m_face->bbox.xMax - m_face->bbox.xMin ) * (Float)style.size * scale + 1 + style.outlineWidth * 2 );
		metrics.maxGlyphHeight = static_cast<Int32>( ( m_face->bbox.yMax - m_face->bbox.yMin ) * (Float)style.size * scale + 1 + style.outlineWidth * 2 );
	}
#endif // RED_USE_FREE_TYPE

	return metrics;
}

Font::CacheDataForFontSize& Font::GetCacheDataForFontSize_NoLock( Uint16 size ) const
{
	// make sure there is hash map created for given font size
	auto cacheForFontSizeIter = m_cache.Find( size );
	if ( cacheForFontSizeIter == m_cache.End() )
	{
		auto insertResult = m_cache.Insert( size, CacheDataForFontSize{ rend::PoolFont() } );
		RED_FATAL_ASSERT( insertResult.IsSuccessful(), "Failed to create glyph metrics cache entry. Font: '%s (%s)', font size: %u", GetFontName(), GetStyleName(), size );
		cacheForFontSizeIter = insertResult.Iterator();
	}

	return cacheForFontSizeIter.Value();
}

Int32 Font::GetGlyphKerning( const FontStyle& style, GlyphIndex prev, GlyphIndex next ) const
{
	Int16 kerning = 0;

#ifdef RED_USE_FREE_TYPE
	if ( FT_HAS_KERNING( m_face ) )
	{
		RED_SCOPE_LOCK( m_lock );

		Font::CacheDataForFontSize& cacheData = GetCacheDataForFontSize_NoLock( style.size );

		const KerningPair pair{ prev, next };

		// find entry in sub-map
		KerningCache& kerningCache = cacheData.m_kerningCache;
		auto iter = kerningCache.Find( pair );
		if ( iter != kerningCache.End() )
		{
			kerning = iter.Value();
		}
		else
		{
			kerning = static_cast<Int16>( ComputeGlyphKerning_NoLock( style, prev, next ) );
			RED_VERIFY( kerningCache.Insert( pair, kerning ).IsSuccessful(), "Failed to insert glyph kerning to cache" );
		}
	}
#endif // RED_USE_FREE_TYPE

	return kerning;
}

Int32 Font::ComputeGlyphKerning_NoLock( const FontStyle& style, GlyphIndex prev, GlyphIndex next ) const
{
#ifdef RED_USE_FREE_TYPE

	if ( !FT_HAS_KERNING( m_face ) )
	{
		return 0;
	}

	FT_Error error = FT_Set_Pixel_Sizes( m_face, 0, static_cast<FT_UInt>( style.size ) );
	if ( error )
	{
		return 0;
	}

	// TODO cache kerning values - it takes a lot of the time
	FT_Vector kerning;
	if ( FT_Get_Kerning( m_face, prev, next, FT_KERNING_UNSCALED, &kerning ) )
	{
		return 0;
	}

	return kerning.x / 64;
#else
	return 0;
#endif // RED_USE_FREE_TYPE
}

void Font::ForceRefreshFontSize_NoLock( const FontStyle & style ) const
{
	#ifdef RED_USE_FREE_TYPE
	FT_Error error = FT_Set_Pixel_Sizes( m_face, 0, static_cast< FT_UInt >( style.size ) );
	if( error )
	{
		return;
	}
	#endif // RED_USE_FREE_TYPE
}

void Font::Lock() const
{
	m_lock.Acquire();
}

void Font::UnLock() const
{
	m_lock.Release();
}

const Font::GlyphMetrics Font::GetGlyphMetrics( const FontStyle& style, GlyphIndex glyphIndex ) const
{
	RED_SCOPE_LOCK( m_lock );

	Font::CacheDataForFontSize& cacheData = GetCacheDataForFontSize_NoLock( style.size );

	GlyphMetrics metrics;

	// find entry in sub-map
	GlyphMetricsCache& cacheForFontSize = cacheData.m_glyphMetricsCache;
	auto iter = cacheForFontSize.Find( glyphIndex );
	if ( iter != cacheForFontSize.End() )
	{
		metrics = iter.Value();
	}
	else
	{
		metrics = ComputeGlyphMetrics_NoLock( style, glyphIndex );
		RED_VERIFY( cacheForFontSize.Insert( glyphIndex, metrics ).IsSuccessful(), "Failed to insert glyph metrics to cache" );
	}

	// adjust for outline
	metrics.rect.m_height += 2 * ( style.outlineWidth + c_textMarigin );
	metrics.rect.m_width += 2 * ( style.outlineWidth + c_textMarigin );
	metrics.rect.m_xOffset -= style.outlineWidth + c_textMarigin;
	metrics.rect.m_yOffset -= style.outlineWidth + c_textMarigin;

	return metrics;
}

const Font::GlyphMetrics Font::ComputeGlyphMetrics_NoLock( const FontStyle& style, GlyphIndex glyphIndex ) const
{
	GlyphMetrics metrics;

#ifdef RED_USE_FREE_TYPE
	FT_Error error = FT_Set_Pixel_Sizes( m_face, 0, static_cast<FT_UInt>( style.size ) );
	if ( error )
	{
		RED_LOG_ERROR( "Unable to set desired font size" );
		return GlyphMetrics();
	}

	if ( FT_Load_Glyph( m_face, glyphIndex, FT_LOAD_FORCE_AUTOHINT ) )
	{
		RED_LOG_ERROR( "Unable to load glyph" );
		return GlyphMetrics();
	}

#define INT16RANGE(param) ( ( param ) <= std::numeric_limits<Int16>::max() && ( param ) >= std::numeric_limits<Int16>::min() )
	RED_ASSERT( INT16RANGE( m_face->glyph->advance.x + 63 ) );
	metrics.advanceWidth = static_cast< Int16 >( ( m_face->glyph->advance.x + 63 ) >> 6 );

	RED_ASSERT( INT16RANGE( m_face->glyph->bitmap_left ) );
	metrics.rect.m_xOffset = m_face->glyph->bitmap_left;

	RED_ASSERT( INT16RANGE( -m_face->glyph->bitmap_top ) );
	metrics.rect.m_yOffset = -m_face->glyph->bitmap_top;

#define UINT16RANGE( param ) ( ( param ) <= std::numeric_limits<Uint16>::max() && ( param  ) >= std::numeric_limits<Uint16>::min() )
	RED_ASSERT( UINT16RANGE( m_face->glyph->metrics.width + 63 ) );
	metrics.rect.m_width = static_cast< Uint16 >( ( m_face->glyph->metrics.width + 63 ) >> 6 );

	RED_ASSERT( UINT16RANGE( m_face->glyph->metrics.height + 63 ) );
	metrics.rect.m_height = static_cast< Uint16 >( ( m_face->glyph->metrics.height + 63 ) >> 6 );
#endif // RED_USE_FREE_TYPE

	return metrics;
}

Bool Font::RenderGlyph_Internal( const FontStyle& style, GlyphIndex glyphIndex, const FontRect& rect, Uint8* outBuffer, Uint8* outOutlineBuffer ) const
{
	const size_t outBufferSizeInPixels = (size_t)rect.m_width * (size_t)rect.m_height;

	RED_SCOPE_LOCK( m_lock );

	red::StopWatch sw;

#ifdef RED_USE_FREE_TYPE
	FT_Error error = FT_Set_Pixel_Sizes( m_face, 0, static_cast<FT_UInt>( style.size ) );
	if ( error )
	{
		RED_LOG_ERROR( "Unable to set desired font size (glyph index = %u)", glyphIndex );
		return false;
	}

	if ( FT_Load_Glyph( m_face, glyphIndex, FT_LOAD_FORCE_AUTOHINT ) )
	{
		RED_LOG_ERROR( "Unable to load glyph (glyph index = %u)", glyphIndex );
		return false;
	}

	// rasterize outline glyph
	if ( style.outlineWidth > 0 )
	{
		FT_Glyph glyph;
		error = FT_Get_Glyph( m_face->glyph, &glyph );
		if ( error )
		{
			RED_LOG_ERROR( "Failed to get glyph (index=%u)", glyphIndex );
			return false;
		}

		if ( glyph->format != FT_GLYPH_FORMAT_BITMAP )
		{
			FT_Library library = FreeTypeLibraryWrapper::GetInstance().GetLibrary();

			// initialize stroker, so you can create outline font
			FT_Stroker stroker;
			FT_Stroker_New( library, &stroker );
			FT_Stroker_Set( stroker, style.outlineWidth * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0 );
			FT_Glyph_StrokeBorder( &glyph, stroker, false, true );

			error = FT_Glyph_To_Bitmap( &glyph, FT_RENDER_MODE_NORMAL, nullptr, true );
			if ( error )
			{
				RED_LOG_ERROR( "Failed to render outline glyph (index=%u)", glyphIndex );
				return false;
			}

			FT_BitmapGlyph bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>( glyph );
			FT_Bitmap& bitmap = bitmapGlyph->bitmap;
			if ( !bitmap.buffer )
			{
				RED_LOG_ERROR( "Failed to render outline glyph (index=%u)", glyphIndex );
				return false;
			}

			// offset the bitmap so the outline and regular glyph are aligned 
			const Int32 offsetX = bitmapGlyph->left - rect.m_xOffset;
			const Int32 offsetY = -( rect.m_yOffset + bitmapGlyph->top );

			for ( Uint32 y = 0; y < bitmap.rows; ++y )
			{
				for ( Uint32 x = 0; x < bitmap.width; ++x )
				{
					// Note: sometimes the stroked glyph bitmap does not match in size with calculated one in ComputeGlyphMetrics_NoLock,
					// thus the check to avoid buffer overrun
					if ( x + offsetX >= 0 && x + offsetX < rect.m_width && y + offsetY >= 0 && y + offsetY < rect.m_height )
					{
						const size_t sourceOffset = y * bitmap.width + x;
						const size_t targetOffset = ( y + offsetY ) * rect.m_width + x + offsetX;
						RED_FATAL_ASSERT( targetOffset < outBufferSizeInPixels, "x=%u, y=%u, rectHeight=%u, rectWidth=%u, bitmapRows=%u, bitmapWidth=%u, outlineWidth=%u",
							x, y, (Uint32)rect.m_height, (Uint32)rect.m_width, (Uint32)bitmap.rows, (Uint32)bitmap.width, (Uint32)style.outlineWidth );

						outOutlineBuffer[ targetOffset ] = bitmap.buffer[ sourceOffset ];
					}
				}
			}

			FT_Stroker_Done( stroker );
		}

		FT_Done_Glyph( glyph );
	}

	// rasterize regular glyph
	{
		FT_Glyph glyph;
		error = FT_Get_Glyph( m_face->glyph, &glyph );
		if ( error )
		{
			RED_LOG_ERROR( "Failed to get glyph (index=%u)", glyphIndex );
			return false;
		}

		if ( glyph->format != FT_GLYPH_FORMAT_BITMAP )
		{
			error = FT_Glyph_To_Bitmap( &glyph, FT_RENDER_MODE_NORMAL, nullptr, true );
			if ( error )
			{
				RED_LOG_ERROR( "Failed to render glyph (index=%u)", glyphIndex );
				return false;
			}

			FT_BitmapGlyph bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>( glyph );
			FT_Bitmap& bitmap = bitmapGlyph->bitmap;
			if ( !bitmap.buffer )
			{
				RED_LOG_ERROR( "Failed to render glyph (index=%u)", glyphIndex );
				return false;
			}

			// offset the bitmap so the outline and regular glyph are aligned 
			const Int32 offsetX = style.outlineWidth + c_textMarigin;
			const Int32 offsetY = style.outlineWidth + c_textMarigin;

			for ( Uint32 y = 0; y < Min<Uint32>( rect.m_height, bitmap.rows ); ++y )
			{
				for ( Uint32 x = 0; x < Min<Uint32>( rect.m_width, bitmap.width ); ++x )
				{
					const size_t sourceOffset = y * bitmap.width + x;
					const size_t targetOffset = ( y + offsetY ) * rect.m_width + x + offsetX;
					RED_FATAL_ASSERT( targetOffset < outBufferSizeInPixels, "x=%u, y=%u, rectHeight=%u, rectWidth=%u, bitmapRows=%u, bitmapWidth=%u, outlineWidth=%u",
						x, y, (Uint32)rect.m_height, (Uint32)rect.m_width, (Uint32)bitmap.rows, (Uint32)bitmap.width, (Uint32)style.outlineWidth );

					outBuffer[ targetOffset ] = bitmap.buffer[ sourceOffset ];
				}
			}
		}

		FT_Done_Glyph( glyph );
	}
#endif // RED_USE_FREE_TYPE

	RED_LOG_SPAM( "Font: Generating glyph bitmap took %.3f ms (glyph=%u, font=%hs, size=%u, outline=%u)",
		sw.GetDeltaMS(), glyphIndex, GetPath().ToDebugString(), style.size, style.outlineWidth );
	return true;
}

const FT_Face & Font::GetFontFace() const
{
	return m_face;
}

} // rend