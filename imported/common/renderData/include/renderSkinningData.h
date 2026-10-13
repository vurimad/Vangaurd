#pragma once
#include "renderObject.h"

enum ESkinningDataMatrixType : Int32
{
	SDMT_3x4Transposed,		//!< 3x4 transposed matrix, as used in "extended" skinning data.
	SDMT_4x4				//!< 4x4 matrix, as used in "simple" skinning data.
};

enum ESkinningDataCustomMatrix
{
	SDCM_EyeOrientationLeft,
	SDCM_EyeOrientationRight,
	SDCM_HeadTransformation,

	SDCM_MAX
};


struct RenderSkinningXForm
{
	Vector4 col0;
	Vector4 col1;
	Vector4 col2;

	static RenderSkinningXForm FromMatrix( const Matrix& mtx )
	{
		const Matrix transposed = mtx.Transposed();

		RenderSkinningXForm xform;
		xform.col0 = transposed.GetRow(0);
		xform.col1 = transposed.GetRow(1);
		xform.col2 = transposed.GetRow(2);
		return xform;
	}

	static Matrix ToMatrix( const RenderSkinningXForm& xform )
	{
		Matrix transposed = Matrix::IDENTITY();
		transposed.SetRow( 0, xform.col0 );
		transposed.SetRow( 1, xform.col1 );
		transposed.SetRow( 2, xform.col2 );

		return transposed.Transposed();
	}


#if 0
	Float pos[3];
	Uint16 xAxis[3];
	Uint16 yAxis[3];
	Uint16 zAxis[3];
	Uint16 extraData;

	static RenderSkinningXForm FromMatrix( const Matrix& mtx )
	{
		RenderSkinningXForm xform;

		const Vector4& pos = mtx.GetTranslationRef();
		xform.pos[0] = pos.X;
		xform.pos[1] = pos.Y;
		xform.pos[2] = pos.Z;

		const Vector4 xAxis = mtx.GetAxisX();
		const Vector4 yAxis = mtx.GetAxisY();
		const Vector4 zAxis = mtx.GetAxisZ();

		constexpr Int32 rounding = _MM_FROUND_TO_NEAREST_INT;
		__m128i xAxisHalf = _mm_cvtps_ph( xAxis.vec, rounding );
		__m128i yAxisHalf = _mm_cvtps_ph( yAxis.vec, rounding );
		__m128i zAxisHalf = _mm_cvtps_ph( zAxis.vec, rounding );

		_mm_storel_epi64( (__m128i*)xform.xAxis, xAxisHalf );
		_mm_storel_epi64( (__m128i*)xform.yAxis, yAxisHalf );
		_mm_storel_epi64( (__m128i*)xform.zAxis, zAxisHalf );

		xform.extraData = 1;
		return xform;
	}

	static Matrix ToMatrix( const RenderSkinningXForm& xform )
	{
		const Vector4 pos( xform.pos );

		Vector4 xAxis, yAxis, zAxis;

		xAxis.vec = _mm_cvtph_ps( _mm_setr_epi16( xform.xAxis[0], xform.xAxis[1], xform.xAxis[2], 0, 0, 0, 0, 0 ) );
		yAxis.vec = _mm_cvtph_ps( _mm_setr_epi16( xform.yAxis[0], xform.yAxis[1], xform.yAxis[2], 0, 0, 0, 0, 0 ) );
		zAxis.vec = _mm_cvtph_ps( _mm_setr_epi16( xform.zAxis[0], xform.zAxis[1], xform.zAxis[2], 0, 0, 0, 0, 0 ) );
		
		Matrix mtx( xAxis, yAxis, zAxis, pos );
		mtx[3][3] = (Float)xform.extraData;

		return mtx;
	}
#endif
};

class RenderSkinningWrite : red::NonCopyable
{

public:
    RenderSkinningWrite( RenderSkinningXForm* beginPtr, Uint32 capacity )
        : m_begin( beginPtr ), m_count( capacity )
    {}

    Bool Set( Uint32 index, const Matrix& matrix, Uint16 extraData = 1 )
    {
		if( index >= m_count )
			return false;

		m_begin[index] = RenderSkinningXForm::FromMatrix( matrix );
		//m_begin[index].extraData = extraData;
        return true;
    }
    Bool IsValid() const { return m_begin && m_count; }

	/// Use it wisely. If stored longer than 1 frame it's invalid.
	/// Used in case where we want to reserve skinning write memory upfront
	/// and keep it for a while until we pass the skinning matrices.
	/// This way we can sort the skinning requests by distance
	/// and later on push skinning matrices from multiple threads.
	/// All this is to avoid flickering characters near camera when out of skinning memory.
	void ExplicitOverride(RenderSkinningWrite other)
	{
		m_begin = other.m_begin;
		m_count = other.m_count;
	}

    Uint32 Count() const { return m_count; }

	RenderSkinningWrite() = delete;
	RenderSkinningWrite(RenderSkinningWrite&&) = default;
	RenderSkinningWrite& operator=(RenderSkinningWrite&&) = delete;

private:
	RenderSkinningXForm* m_begin;
    Uint32 m_count;
};


struct SkinningDataRequest
{
    SkinningDataRequest( Uint32 elements, Uint8 whichLod )
        : nbElements( elements ), lod( whichLod )
    {}
    const Uint32 nbElements;
    const Uint8 lod;

    static SkinningDataRequest NoLOD( Uint32 nbElements ) { return SkinningDataRequest( nbElements, 0xFF ); }
};

/// Skinning data
class IRenderSkinningData : public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolSkinningData );

public:
	virtual Uint32 GetMatrixCount() const = 0;
	virtual void AdvanceWrite() {};
    virtual RenderSkinningWrite GetWriteData( const SkinningDataRequest& request ) = 0;

	virtual void MarkAsStatic() {}
	virtual void KeepDataAlive( Bool value ) const {}
	virtual void IgnorePreviousFrameData( Bool value ) const {}
	virtual void SetCustomHeadData( const Vector4 &pos, const Vector4 &frontDirection, const Vector4 &upDirection ) {}
	virtual void ResetCustomHeadData() {}

	virtual void SetCustomMatrix( ESkinningDataCustomMatrix type, const Matrix &inMatrix ) {}
	virtual Bool GetCustomMatrix( ESkinningDataCustomMatrix type, Matrix &outMatrix ) const { return false; }
	virtual Bool GetCustomHeadData( Vector4 &outPos, Vector4 &outFrontDirection, Vector4 &outUpDirection ) const { return false; }

    virtual CName GetCategory() const { return CName::NONE(); }
    virtual Uint8 GetLODLevel() const { return 0xFF; }
};

class IRenderFloatTrackData : public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolSkinningData );

public:
	virtual Uint32 GetFloatCount() const = 0;
	virtual void AdvanceWrite() {};
	virtual void* GetWriteData() const=0;

	virtual void WriteTrackData( Uint32 numTracks, red::ArraySpan<const Int16> trackMapping, const Float* trackData ) = 0;
};

namespace utils
{
	RED_FORCE_INLINE Uint32 GetMatrixNumFloats( ESkinningDataMatrixType type )
	{
		if ( type == SDMT_3x4Transposed )
		{
			return 3 * 4;
		}
		else
		{
			RED_ASSERT( SDMT_4x4 == type );
			return 4 * 4;
		}
	}

	RED_FORCE_INLINE void CopyMatrix( Float* dest, const Matrix& source, ESkinningDataMatrixType destType )
	{
		if ( destType == SDMT_3x4Transposed )
		{
			dest[0] = source[0][0];
			dest[1] = source[1][0];
			dest[2] = source[2][0];
			dest[3] = source[3][0];

			dest[4] = source[0][1];
			dest[5] = source[1][1];
			dest[6] = source[2][1];
			dest[7] = source[3][1];

			dest[8] = source[0][2];
			dest[9] = source[1][2];
			dest[10] = source[2][2];
			dest[11] = source[3][2];
		}
		else
		{
			RED_ASSERT( SDMT_4x4 == destType );
			red::Memcpy( dest, source.AsFloat(), sizeof( Matrix ) );
		}
	}

	RED_FORCE_INLINE void CopyMatrix( Matrix& dest, const Float* source, ESkinningDataMatrixType sourceType )
	{
		if ( sourceType == SDMT_3x4Transposed )
		{
			for ( Uint32 i = 0; i < 3; ++i )
			{
				dest[0][i] = source[4 * i + 0];
				dest[1][i] = source[4 * i + 1];
				dest[2][i] = source[4 * i + 2];
				dest[3][i] = source[4 * i + 3];
			}
			dest[0][3] = 0;
			dest[1][3] = 0;
			dest[2][3] = 0;
			dest[3][3] = 1;
		}
		else
		{
			RED_ASSERT( SDMT_4x4 == sourceType );
			red::Memcpy( dest.AsFloat(), source, sizeof( Matrix ) );
		}
	}
}