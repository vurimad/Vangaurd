//
// Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
//	

#pragma once

#include "../../redReflection/include/resource.h"
#include "../../redJobs2/include/jobSystem.h"

struct FT_FaceRec_;
using FT_Face = FT_FaceRec_ * ;

struct FT_LibraryRec_;
using FT_Library = FT_LibraryRec_ * ;

struct FT_MemoryRec_;
using FT_Memory = FT_MemoryRec_ * ;

namespace rend {

class Font;
struct FontStyle;
using FontPtr = THandle< Font >;
using FontWeakPtr = WeakHandle< Font >;

struct FontRect
{
	Int16 m_xOffset = 0;
	Int16 m_yOffset = 0;
	Uint16 m_width = 0;
	Uint16 m_height = 0;
};

using GlyphIndex = Uint32;

struct GlyphInfo
{
	GlyphIndex glyphIndex = 0;
	Float x = 0.0f;
	Float y = 0.0f;
	Float width = 0.0f;
	Float height = 0.0f;
	Float size = 0.0f;
	math::Color color = math::Color::WHITE();
};

class RENDER_DATA_API FreeTypeLibraryWrapper
{
public:
	static FreeTypeLibraryWrapper& GetInstance();
	RED_FORCE_INLINE FT_Library GetLibrary() const { return m_library; }
	RED_FORCE_INLINE red::LightMutex& GetMutex() { return m_mutex; }

private:
	FreeTypeLibraryWrapper();
	~FreeTypeLibraryWrapper();

	FT_Memory m_memory;
	FT_Library m_library;
	red::LightMutex m_mutex;
};

// wrapper for a font resource
// can output glyph bitmap for given font size and character codepoint
class RENDER_DATA_API Font : public CResource
{
	RTTI_DECLARE_TYPE( Font );
	IMPLEMENT_RESOURCE_INTERFACE( "fnt", "fnt", "Font" ); // should be 'font' for clarity, but it's reserved by CFont...
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolFont );

public:
	struct GlyphMetrics
	{
		Int16 advanceWidth = 0;
		FontRect rect;
	};

	struct Metrics
	{
		Int32 ascender = 0;
		Int32 descender = 0;
		Int32 lineSpacing = 0;
		Uint32 maxGlyphWidth = 0;
		Uint32 maxGlyphHeight = 0;
	};

	Font();
	~Font();

	const char* GetFontName() const;
	const char* GetStyleName() const;

	bool SetFontBuffer( red::DynArray< Uint8 >&& buffer );

	virtual void OnPostLoad( const PostLoadContext& context ) override final;

	// find glyph index given a code point
	// #TODO handle multiple glyphs per codepoint (e.g. Arabic language)
	GlyphIndex GetGlyphIndexForChar( red::CodePoint codePoint ) const;

	// get overall font metrics
	const Metrics GetMetrics( const FontStyle& style ) const;

	// get single glyph metrics
	const GlyphMetrics GetGlyphMetrics( const FontStyle& style, GlyphIndex glyphIndex ) const;

	// get kerning value between two glyphs
	Int32 GetGlyphKerning( const FontStyle& style, GlyphIndex prev, GlyphIndex next ) const;

	// rasterize glyph, very slow, should be used by glyph cache only
	// Important: output buffer is a two-channel buffer (in order to support outlines):
	// first channel is regular glyph, second contains beveled version of it
	Bool RenderGlyph_Internal( const FontStyle& style, GlyphIndex glyphIndex, const FontRect& rect, Uint8* outBuffer, Uint8* outOutlineBuffer ) const;

	void ForceRefreshFontSize_NoLock( const FontStyle& style ) const;
	void Lock() const;
	void UnLock() const;

	const FT_Face &GetFontFace() const;
private:

	struct KerningPair
	{
		GlyphIndex leftGlyphIndex;
		GlyphIndex rightGlyphIndex;

		RED_FORCE_INLINE bool operator == ( const KerningPair& other ) const
		{
			return leftGlyphIndex == other.leftGlyphIndex && rightGlyphIndex == other.rightGlyphIndex;
		}

		// Note: must be fast, because it's executed per-character
		RED_FORCE_INLINE Uint32 CalcHash() const
		{
			return leftGlyphIndex | ( rightGlyphIndex << 16 );
		}
	};

	using GlyphMetricsCache = red::HashMap< GlyphIndex, GlyphMetrics >;
	using KerningCache = red::HashMap< KerningPair, Int16 >;

	struct CacheDataForFontSize
	{
		// #TODO separate pool for font caches?
		GlyphMetricsCache m_glyphMetricsCache{ rend::PoolFont() };
		KerningCache m_kerningCache{ rend::PoolFontCache() };
	};

	bool Initialize();
	void Release();

	CacheDataForFontSize& GetCacheDataForFontSize_NoLock( Uint16 size ) const;

	// compute single glyph metrics
	// Note: this does not take outline into account!
	const GlyphMetrics ComputeGlyphMetrics_NoLock( const FontStyle& style, GlyphIndex glyphIndex ) const;

	// compute kerning value between two glyphs
	// Note: this does not take outline into account!
	Int32 ComputeGlyphKerning_NoLock( const FontStyle& style, GlyphIndex prev, GlyphIndex next ) const;

	DataBuffer m_fontBuffer{ rend::PoolFont() };

	mutable red::RWSpinLock m_lock;

	FT_Face	m_face;

	mutable red::HashMap< Uint16, CacheDataForFontSize > m_cache;
};

} // rend