#pragma once

union GridCoords
{
    // 21 bit for relative coordinate (2097152 should be enough)
    static constexpr Uint32 c_numBits = 21;
    Uint64 hash;
    struct
    {
        Uint64 x : c_numBits;
        Uint64 y : c_numBits;
        Uint64 z : c_numBits;
        Uint64 w : 1;
    };

    GridCoords() {}
    GridCoords( Uint32 vx, Uint32 vy, Uint32 vz )
        : x( vx ), y( vy ), z( vz ), w( 1 ) {}

    Uint32 CalcHash() const
    {
        static constexpr Uint32 c_prime1 = 73856093;
        static constexpr Uint32 c_prime2 = 19349663;
        static constexpr Uint32 c_prime3 = 83492791;
        static constexpr Uint32 c_prime4 = 402653189;
        return (Uint32)( (x*c_prime1) ^ (y*c_prime2) ^ (z*c_prime3) ^ (w*c_prime4) );
    }

	RED_INLINE Bool operator <  ( const GridCoords& other ) const { return hash < other.hash; }
    RED_INLINE Bool operator == ( const GridCoords& other ) const { return hash == other.hash; }
    RED_INLINE Bool operator != ( const GridCoords& other ) const { return hash != other.hash; }

	static RED_INLINE GridCoords Min( const GridCoords& left, const GridCoords& right )
	{
		GridCoords result;
		result.x = ::Min( left.x, right.x );
		result.y = ::Min( left.y, right.y );
		result.z = ::Min( left.z, right.z );
		return result;
	}
	static RED_INLINE GridCoords Max( const GridCoords& left, const GridCoords& right )
	{
		GridCoords result;
		result.x = ::Max( left.x, right.x );
		result.y = ::Max( left.y, right.y );
		result.z = ::Max( left.z, right.z );
		return result;
	}

};

RED_INLINE GridCoords ComputeGridCoords( const Vector3& v, const Vector3& cellSizeInv )
{
    constexpr Uint32 c_maxValueUint = 1 << GridCoords::c_numBits;
    const Float c_maxValue = (Float)(c_maxValueUint);

    if( v.X >= c_maxValue || v.Y >= c_maxValue || v.Z >= c_maxValue )
    {
        RED_LOG_WARNING( "Invalid cell coords! Values will be clambed. Probably the cell is to big (input: %.3f, %.3f, %.3f", v.X, v.Y, v.Z );
    }

    const Vector3 vGrid = v * cellSizeInv;

    GridCoords result = { 0, 0, 0 };
    result.x = Min( (Uint32)vGrid.X, c_maxValueUint );
    result.y = Min( (Uint32)vGrid.Y, c_maxValueUint );
    result.z = Min( (Uint32)vGrid.Z, c_maxValueUint );
    return result;
}

RED_INLINE GridCoords ComputeGridCoords( const Vector3& input, const Vector3& base, const Vector3& cellSizeInv )
{
    const Vector3 v = (input - base);
    return ComputeGridCoords( v, cellSizeInv );
}
RED_FORCE_INLINE GridCoords ComputeGridCoords( const Vector3& input, const Vector3& base, Float cellSizeInv )
{
    return ComputeGridCoords( input, base, Vector3( cellSizeInv ) );
}

RED_INLINE Vector3 ComputePosition( const GridCoords& input, const Vector3& base, const Vector3& cellSize )
{
    Vector3 result;
    result.X = (Float)input.x * cellSize.X;
    result.Y = (Float)input.y * cellSize.Y;
    result.Z = (Float)input.z * cellSize.Z;
    return base + result;
}
RED_FORCE_INLINE Vector3 ComputePosition( const GridCoords& input, const Vector3& base, Float cellSize )
{
    return ComputePosition( input, base, Vector3( cellSize ) );
}


struct QuantizedEntry
{
	GridCoords m_coords{0,0,0};
	Uint32 m_index;
};
using QuantizedEntryArray = red::DynArray<QuantizedEntry>;
using QuantizedEntryMap = red::Map<GridCoords, QuantizedEntryArray>;


struct GridHelper
{
	Uint32 m_x;
	Uint32 m_y;
	Uint32 m_z;

	//
	GridHelper()
		: m_x( 0 ), m_y( 0 ), m_z( 0 )
	{}

	GridHelper( Uint32 w, Uint32 h, Uint32 d )
		: m_x( w ), m_y( h ), m_z( d )
	{}

	//
	Uint32 NumCells() const
	{
		return m_x * m_y * m_z;
	}
	Int32 Index( Uint32 x, Uint32 y, Uint32 z ) const
	{
		return x + y * m_x + z * m_x*m_y;
	}
	void Coords( Uint32 xyz[3], Uint32 idx ) const
	{
		Coords( &xyz[0], &xyz[1], &xyz[2], idx );
	}

	void Coords( Uint32* x, Uint32* y, Uint32* z, Uint32 idx ) const
	{
		const Int32 wh = m_x * m_y;
		const Int32 index_mod_wh = idx % wh;

		x[0] = (index_mod_wh % m_x);
		y[0] = (index_mod_wh / m_x);
		z[0] = (idx / wh);
	}
};
