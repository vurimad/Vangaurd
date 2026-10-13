/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// Material code chunk
class CodeChunk
{
protected:
	red::DynArray< AnsiChar >				m_code{ red::PoolEngine() };
	Bool									m_const;

public:
	static CodeChunk EMPTY;		// Empty code chunk

public:
	// Default constructor
	RED_INLINE CodeChunk()
		: m_const( true )
	{};

	// Copy Constructor
	RED_INLINE CodeChunk( const CodeChunk& chunk )
		: m_code( chunk.m_code )
		, m_const( chunk.m_const )
	{};

	// Move constructor
	RED_INLINE CodeChunk( CodeChunk&& chunk )
	{
		m_const = chunk.m_const;
		m_code = std::move( chunk.m_code );
	};

	// Construct from float
	RED_INLINE CodeChunk( Float value )
		: m_const( true )
	{
		AnsiChar text[ 64 ];
		red::SNPrintF( text, RED_ARRAY_COUNT( text ), "(%f)", value );
		Uint32 length = static_cast< Uint32 >( red::Strlen( text ) );
		m_code.Resize( length+1 );
		red::Memcpy( m_code.TypedData(), text, length+1 );	
	}	

	// Construct from uint
	RED_INLINE CodeChunk( Uint32 value )
		: m_const( true )
	{
		AnsiChar text[ 64 ];
		red::SNPrintF( text, RED_ARRAY_COUNT( text ), "(%d)", value );
		Uint32 length = static_cast< Uint32 >( red::Strlen( text ) );
		m_code.Resize( length+1 );
		red::Memcpy( m_code.TypedData(), text, length+1 );	
	}	

	// Construct from vector
	RED_INLINE CodeChunk( const Vector4& value )
		: m_const( true )
	{
		AnsiChar text[ 64 ];
		red::SNPrintF( text, RED_ARRAY_COUNT( text ), "float4( %f, %f, %f, %f )", value.X, value.Y, value.Z, value.W );
		Uint32 length = static_cast< Uint32 >( red::Strlen( text ) );
		m_code.Resize( length+1 );
		red::Memcpy( m_code.TypedData(), text, length+1 );		
	}

	// Construct from string
	RED_INLINE CodeChunk( const AnsiChar* code, Bool isConst = true )
		: m_const( isConst )
	{
		ASSERT( code );
		Uint32 length = static_cast< Uint32 >( red::Strlen( code ) );
		m_code.Resize( length+1 );
		red::Memcpy( m_code.TypedData(), code, length+1 );
	}

	// Convert to string
	RED_INLINE const AnsiChar* AsChar() const
	{
		const AnsiChar* text = m_code.TypedData();
		return text ? text : "";
	}

	// Check if not empty
	RED_INLINE operator Bool() const
	{
		return !IsEmpty();
	}

	// Check if empty, explicit way
	RED_INLINE Bool IsEmpty() const
	{
		return m_code.Size() <= 1;
	}

	// Check if code is const
	RED_INLINE Bool IsConst() const
	{
		return m_const;
	}

	// Change code constness
	RED_INLINE void SetConst( Bool isConst )
	{
		m_const = isConst;
	}

	// Move assignment operator
	RED_INLINE CodeChunk& operator=( CodeChunk&& chunk )
	{
		m_const = chunk.m_const;
		m_code = std::move( chunk.m_code );

		return *this;
	}

	// Copy assignment operator
	RED_INLINE CodeChunk& operator=( const CodeChunk& chunk )
	{
		m_const = chunk.m_const;
		m_code = chunk.m_code;

		return *this;
	}

	// Compare value
	RED_INLINE Bool operator==( const AnsiChar* dupa ) const
	{
		ASSERT( dupa );
		return 0 == red::StrcmpNC( dupa, AsChar() );
	}

	// Compare value
	RED_INLINE Bool operator!=( const AnsiChar* dupa ) const
	{
		ASSERT( dupa );
		return 0 != red::StrcmpNC( dupa, AsChar() );
	}

	// Compare value
	RED_INLINE Bool operator==( const CodeChunk& dupa ) const
	{
		return 0 == red::StrcmpNC( AsChar(), dupa.AsChar() );
	}

	// Compare value
	RED_INLINE Bool operator!=( const CodeChunk& dupa ) const
	{
		return 0 != red::StrcmpNC( AsChar(), dupa.AsChar() );
	}

public:
	// Create string from formatted expression
	static CodeChunk Printf( Bool isConst, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
	{
		va_list arglist;
		va_start(arglist, format);

		int len = static_cast< int >( red::Strlen(format) + 2048 );

		// GET NEAREST POWER OF 2
		len -= 1;
		len |= len >> 16;
		len |= len >> 8;
		len |= len >> 4;
		len |= len >> 2;
		len |= len >> 1;
		len += 1; 

		AnsiChar* formattedBuf = RED_NEW_ARRAY( AnsiChar, len, red::PoolEngine );
		int n = red::VSNPrintF( formattedBuf, len, format, arglist );
		ASSERT( n <= len );
		va_end(arglist);
		CodeChunk ret(formattedBuf, isConst);
		RED_DELETE_ARRAY( formattedBuf, len, red::PoolEngine );
		return ret;
	}

public:
	// Swizzle
	RED_INLINE CodeChunk x() const { return Printf( m_const, "(%s).x", AsChar() ); }
	RED_INLINE CodeChunk y() const { return Printf( m_const, "(%s).y", AsChar() ); }
	RED_INLINE CodeChunk z() const { return Printf( m_const, "(%s).z", AsChar() ); }
	RED_INLINE CodeChunk w() const { return Printf( m_const, "(%s).w", AsChar() ); }

	RED_INLINE CodeChunk xx() const { return Printf( m_const, "(%s).xx", AsChar() ); }
	RED_INLINE CodeChunk xy() const { return Printf( m_const, "(%s).xy", AsChar() ); }
	RED_INLINE CodeChunk xz() const { return Printf( m_const, "(%s).xz", AsChar() ); }
	RED_INLINE CodeChunk xw() const { return Printf( m_const, "(%s).xw", AsChar() ); }
	RED_INLINE CodeChunk yx() const { return Printf( m_const, "(%s).yx", AsChar() ); }
	RED_INLINE CodeChunk yy() const { return Printf( m_const, "(%s).yy", AsChar() ); }
	RED_INLINE CodeChunk yz() const { return Printf( m_const, "(%s).yz", AsChar() ); }
	RED_INLINE CodeChunk yw() const { return Printf( m_const, "(%s).yw", AsChar() ); }
	RED_INLINE CodeChunk zx() const { return Printf( m_const, "(%s).zx", AsChar() ); }
	RED_INLINE CodeChunk zy() const { return Printf( m_const, "(%s).zy", AsChar() ); }
	RED_INLINE CodeChunk zz() const { return Printf( m_const, "(%s).zz", AsChar() ); }
	RED_INLINE CodeChunk zw() const { return Printf( m_const, "(%s).zw", AsChar() ); }
	RED_INLINE CodeChunk wx() const { return Printf( m_const, "(%s).wx", AsChar() ); }
	RED_INLINE CodeChunk wy() const { return Printf( m_const, "(%s).wy", AsChar() ); }
	RED_INLINE CodeChunk wz() const { return Printf( m_const, "(%s).wz", AsChar() ); }
	RED_INLINE CodeChunk ww() const { return Printf( m_const, "(%s).ww", AsChar() ); }

	RED_INLINE CodeChunk xyz() const { return Printf( m_const, "(%s).xyz", AsChar() ); }
	RED_INLINE CodeChunk xxx() const { return Printf( m_const, "(%s).xxx", AsChar() ); }
	RED_INLINE CodeChunk yyy() const { return Printf( m_const, "(%s).yyy", AsChar() ); }
	RED_INLINE CodeChunk zzz() const { return Printf( m_const, "(%s).zzz", AsChar() ); }
	RED_INLINE CodeChunk www() const { return Printf( m_const, "(%s).www", AsChar() ); }	

	RED_INLINE CodeChunk xyxy() const { return Printf( m_const, "(%s).xyxy", AsChar() ); }

	RED_INLINE CodeChunk xyzw() const { return Printf( m_const, "(%s).xyzw", AsChar() ); }
	RED_INLINE CodeChunk xxxx() const { return Printf( m_const, "(%s).xxxx", AsChar() ); }
	RED_INLINE CodeChunk yyyy() const { return Printf( m_const, "(%s).yyyy", AsChar() ); }
	RED_INLINE CodeChunk zzzz() const { return Printf( m_const, "(%s).zzzz", AsChar() ); }
	RED_INLINE CodeChunk wwww() const { return Printf( m_const, "(%s).wwww", AsChar() ); }

	// Math
	RED_INLINE CodeChunk operator+( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) + (%s)", AsChar(), other.AsChar() ); }
	RED_INLINE CodeChunk operator-( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) - (%s)", AsChar(), other.AsChar() ); }
	RED_INLINE CodeChunk operator/( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) / (%s)", AsChar(), other.AsChar() ); }
	RED_INLINE CodeChunk operator*( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) * (%s)", AsChar(), other.AsChar() ); }
	RED_INLINE CodeChunk operator>( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) > (%s)", AsChar(), other.AsChar() ); }
	RED_INLINE CodeChunk operator<( const CodeChunk& other ) const { return Printf( m_const && other.m_const, "(%s) < (%s)", AsChar(), other.AsChar() ); }

	// Math with float
	RED_INLINE CodeChunk operator+( const Float other ) const { return Printf( m_const, "(%s) + %f", AsChar(), other ); }
	RED_INLINE CodeChunk operator-( const Float other ) const { return Printf( m_const, "(%s) - %f", AsChar(), other ); }
	RED_INLINE CodeChunk operator/( const Float other ) const { return Printf( m_const, "(%s) / %f", AsChar(), other ); }
	RED_INLINE CodeChunk operator*( const Float other ) const { return Printf( m_const, "(%s) * %f", AsChar(), other ); }


	RED_INLINE CodeChunk operator%( const Uint32 other ) const { return Printf( m_const, "(%s) %% %u", AsChar(), other ); }

	// Unary operators
	RED_INLINE CodeChunk Saturate() const { return Printf( m_const, "saturate(%s)", AsChar() ); }
	RED_INLINE CodeChunk Length() const { return Printf( m_const, "length(%s)", AsChar() ); }
	RED_INLINE CodeChunk Normalize() const { return Printf( m_const, "normalize(%s)", AsChar() ); }
	RED_INLINE CodeChunk operator-() const { return Printf( m_const, "(-(%s))", AsChar() ); }

	// Lookup
	RED_INLINE CodeChunk operator[]( const CodeChunk& index ) const { return CodeChunk::Printf( m_const && index.m_const, "(%s)[ %s ]", AsChar(), index.AsChar() ); }
};

/// Operators
namespace CodeChunkOperators
{
	// Operators
	static RED_INLINE CodeChunk Abs( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "abs(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk All( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "all(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk Any( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "any(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk Saturate( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "saturate(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk Length( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "length(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk Normalize( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "normalize(%s)", a.AsChar() ); }
	static RED_INLINE CodeChunk Add( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "add( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Mul( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "mul( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Div( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "div( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Min( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "min( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Max( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "max( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Dot2( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "dot( (%s).xy, (%s).xy )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Dot3( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "dot( (%s).xyz, (%s).xyz )", a.AsChar(), b.AsChar() ); }	
	static RED_INLINE CodeChunk Dot4( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "dot( (%s).xyzw, (%s).xyzw )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Cross( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "cross( (%s).xyz, (%s).xyz )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Pow( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "pow( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Sqrt( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "sqrt( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Log2( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "log2( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Exp( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "exp( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Exp2( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "exp2( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Clamp( const CodeChunk& x, const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( x.IsConst() && a.IsConst() && b.IsConst(), "clamp( %s, %s, %s)", x.AsChar(), a.AsChar(), b.AsChar() ); } 
	static RED_INLINE CodeChunk Lerp( const CodeChunk& x, const CodeChunk& y, const CodeChunk& s ) { return CodeChunk::Printf( x.IsConst() && y.IsConst() && s.IsConst(), "lerp( %s, %s, %s)", x.AsChar(), y.AsChar(), s.AsChar() ); } 
	static RED_INLINE CodeChunk SmoothStep( const CodeChunk& _min, const CodeChunk& _max, const CodeChunk& x ) { return CodeChunk::Printf( _min.IsConst() && _max.IsConst() && x.IsConst(), "smoothstep( %s, %s, %s)", _min.AsChar(), _max.AsChar(), x.AsChar() ); } 
	static RED_INLINE CodeChunk Mad( const CodeChunk& a, const CodeChunk& b, const CodeChunk& c ) { return CodeChunk::Printf( a.IsConst() && b.IsConst() && c.IsConst(), "(%s) * (%s) + (%s)", a.AsChar(), b.AsChar(), c.AsChar() ); } 
	static RED_INLINE CodeChunk Sin( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "MSin( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Cos( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "MCos( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Tan( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "tan( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk ArcSin( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "asin( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk ArcCos( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "acos( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk ArcTan( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "atan( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk ArcTan2( const CodeChunk& y, const CodeChunk& x ) { return CodeChunk::Printf( y.IsConst() && x.IsConst(), "atan2( %s, %s )", y.AsChar(), x.AsChar() ); }
	static RED_INLINE CodeChunk Frac( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "frac( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Floor( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "floor( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Sign( const CodeChunk& a) { return CodeChunk::Printf( a.IsConst(), "sign( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Reflect( const CodeChunk& v, const CodeChunk& norm ) { return v.xyz() - CodeChunk( 2.0f ) * Dot3( v, norm ) * norm.xyz(); }
	static RED_INLINE CodeChunk Float2( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "float2( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Float2( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "float2( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Float3( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "float3( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Float3( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "float3( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Float3( const CodeChunk& a, const CodeChunk& b, const CodeChunk& c ) { return CodeChunk::Printf( a.IsConst() && b.IsConst() && c.IsConst(), "float3( %s, %s, %s )", a.AsChar(), b.AsChar(), c.AsChar() ); }
	static RED_INLINE CodeChunk Float4( const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "float4( %s )", a.AsChar() ); }
	static RED_INLINE CodeChunk Float4( const CodeChunk& a, const CodeChunk& b ) { return CodeChunk::Printf( a.IsConst() && b.IsConst(), "float4( %s, %s )", a.AsChar(), b.AsChar() ); }
	static RED_INLINE CodeChunk Float4( const CodeChunk& a, const CodeChunk& b, const CodeChunk& c ) { return CodeChunk::Printf( a.IsConst() && b.IsConst() && c.IsConst(), "float4( %s, %s, %s )", a.AsChar(), b.AsChar(), c.AsChar() ); }
	static RED_INLINE CodeChunk Float4( const CodeChunk& a, const CodeChunk& b, const CodeChunk& c, const CodeChunk& d ) { return CodeChunk::Printf( a.IsConst() && b.IsConst() && c.IsConst() && d.IsConst(),  "float4( %s, %s, %s, %s )", a.AsChar(), b.AsChar(), c.AsChar(), d.AsChar() ); }
	static RED_INLINE CodeChunk DDX( const CodeChunk& uv ) { return CodeChunk::Printf( false, "ddx( %s )", uv.AsChar() ); }
	static RED_INLINE CodeChunk DDY( const CodeChunk& uv ) { return CodeChunk::Printf( false, "ddy( %s )", uv.AsChar() ); }

	static RED_INLINE CodeChunk Tex2D( const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "SYS_SAMPLE( %s, (%s).xy )", sampler.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2D( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "%s.Sample( %s, (%s).xy )", texture.AsChar(), sampler.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2DArray( const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& arrayIndex ) { return CodeChunk::Printf( false, "t_%s.Sample( s_%s, float3( (%s).xy, %s ) )", sampler.AsChar(), sampler.AsChar(), uv.AsChar(), arrayIndex.AsChar() ); }
	static RED_INLINE CodeChunk Tex2DArray( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& arrayIndex ) { return CodeChunk::Printf( false, "%s.Sample( %s, float3( (%s).xy, %s ) )", texture.AsChar(), sampler.AsChar(), uv.AsChar(), arrayIndex.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dproj( const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "t_%s.Sample( s_%s, (%s).xy/(%s).w )", sampler.AsChar(), sampler.AsChar(), uv.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dproj( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "%s.Sample( %s, (%s).xy/(%s).w )", texture.AsChar(), sampler.AsChar(), uv.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dlod( const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "SYS_SAMPLE_LEVEL( %s, (%s).xy, (%s).z )", sampler.AsChar(), uv.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dlod( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv ) { return CodeChunk::Printf( false, "SAMPLE_LEVEL( %s, %s, (%s).xy, (%s).z )", texture.AsChar(), sampler.AsChar(), uv.AsChar(), uv.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dgrad( const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& ddx, const CodeChunk& ddy  ) { return CodeChunk::Printf( false, "SAMPLE_GRADIENT( t_%s, s_%s, (%s).xy, (%s).xy, (%s).xy )", sampler.AsChar(), sampler.AsChar(), uv.AsChar(), ddx.AsChar(), ddy.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dgrad( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& ddx, const CodeChunk& ddy  ) { return CodeChunk::Printf( false, "SAMPLE_GRADIENT( %s, %s, (%s).xy, %s.xy, %s.xy )", texture.AsChar(), sampler.AsChar(), uv.AsChar(), ddx.AsChar(), ddy.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dbias( const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& bias ) { return CodeChunk::Printf( false, "t_%s.SampleBias( s_%s, (%s).xy, %s.x )", sampler.AsChar(), sampler.AsChar(), uv.AsChar(), bias.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dbias( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uv, const CodeChunk& bias ) { return CodeChunk::Printf( false, "%s.SampleBias( %s, (%s).xy, %s.x )", texture.AsChar(), sampler.AsChar(), uv.AsChar(), bias.AsChar() ); }
	static RED_INLINE CodeChunk Tex2Dload( const CodeChunk& texture, const CodeChunk& uv, const CodeChunk& level ) { return CodeChunk::Printf( false, "t_%s.Load( float3( (%s).xy, (%s).x ) )", texture.AsChar(), uv.AsChar(), level.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBE( const CodeChunk& sampler, const CodeChunk& uvw ) { return CodeChunk::Printf( false, "t_%s.Sample( s_%s, (%s).xyz )", sampler.AsChar(), sampler.AsChar(), uvw.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBE( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uvw ) { return CodeChunk::Printf( false, "%s.Sample( %s, (%s).xyz )", texture.AsChar(), sampler.AsChar(), uvw.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBEbias( const CodeChunk& sampler, const CodeChunk& uvw, const CodeChunk& bias ) { return CodeChunk::Printf( false, "t_%s.SampleBias( s_%s, (%s).xyz, %s.x )", sampler.AsChar(), sampler.AsChar(), uvw.AsChar(), bias.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBEbias( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uvw, const CodeChunk& bias ) { return CodeChunk::Printf( false, "%s.SampleBias( %s, (%s).xyz, %s.x )", texture.AsChar(), sampler.AsChar(), uvw.AsChar(), bias.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBElod( const CodeChunk& sampler, const CodeChunk& uvw, const CodeChunk& lod ) { return CodeChunk::Printf( false, "SYS_SAMPLE_LEVEL( %s, (%s).xyz, %s.x )", sampler.AsChar(), uvw.AsChar(), lod.AsChar() ); }
	static RED_INLINE CodeChunk TexCUBElod( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uvw, const CodeChunk& lod ) { return CodeChunk::Printf( false, "SAMPLE_LEVEL( %s, %s, (%s).xyz, %s.x )", texture.AsChar(), sampler.AsChar(), uvw.AsChar(), lod.AsChar() ); }
	static RED_INLINE CodeChunk Tex3D( const CodeChunk& sampler, const CodeChunk& uvw ) { return CodeChunk::Printf( false, "t_%s.Sample( s_%s, (%s).xyz )", sampler.AsChar(), sampler.AsChar(), uvw.AsChar() ); }
	static RED_INLINE CodeChunk Tex3D( const CodeChunk& texture, const CodeChunk& sampler, const CodeChunk& uvw ) { return CodeChunk::Printf( false, "%s.Sample( %s, (%s).xyz )", texture.AsChar(), sampler.AsChar(), uvw.AsChar() ); }
	static RED_INLINE CodeChunk ConditionalOperator( const CodeChunk& condition, const CodeChunk& codeForTrue, const CodeChunk& codeForFalse ) 
	{
		return CodeChunk::Printf( condition.IsConst() && codeForTrue.IsConst() && codeForFalse.IsConst(),
			"StaticBranch(%s, %s, %s)", condition.AsChar(), codeForTrue.AsChar(), codeForFalse.AsChar() ); 
	}

	// Math with float
	static RED_INLINE CodeChunk operator+( const Float other, const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "%f + (%s)", other, a.AsChar() ); }
	static RED_INLINE CodeChunk operator-( const Float other, const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "%f - (%s)", other, a.AsChar() ); }
	static RED_INLINE CodeChunk operator/( const Float other, const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "%f / (%s)", other, a.AsChar() ); }
	static RED_INLINE CodeChunk operator*( const Float other, const CodeChunk& a ) { return CodeChunk::Printf( a.IsConst(), "%f * (%s)", other, a.AsChar() ); }

};