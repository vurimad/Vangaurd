#pragma once

//********************************************************************************************//
//********************************************************************************************//
// Mini vector lib

namespace mini
{
	namespace internals
	{
		template <typename T, Uint32 N>
		struct vec;

		template <typename T>
		struct vec<T,2>
		{
			union
			{
				struct { T x, y; };
				T		v[2];
			};

			inline vec() {}
			inline vec( T a ) : x( a ), y( a ) {}
			inline vec( T a, T b = 0 ) : x( a ), y( b ) {}

			template <typename O, Uint32 M>
			inline vec( const vec<O,M>& o ) : x( T(o.x) ), y( T(o.y) ) {}

			inline T& 		operator[]( Uint32 idx ) { return v[idx]; }
			inline const T 	operator[]( Uint32 idx ) const { return v[idx]; }
		};

		template <typename T>
		struct vec<T,3>
		{
			union
			{
				struct { T x, y, z; };
				T		v[3];
			};

			inline vec() {}
			inline vec( T a ) : x( a ), y( a ), z( a ) {}
			inline vec( T a, T b, T c ) : x( a ), y( b ), z( c ) {}

			template <typename O, Uint32 M>
			inline vec( const vec<O, M>& o ) : x( T(o.x) ), y( T(o.y) ), z( T(o.z) ) {}

			inline T& 		operator[]( Uint32 idx ) { return v[idx]; }
			inline const T 	operator[]( Uint32 idx ) const { return v[idx]; }
		};

		template <typename T>
		struct vec<T,4>
		{
			union
			{
				struct { T x, y, z, w; };
				T		v[4];
			};

			inline vec() {}
			inline vec( T a ) : x( a ), y( a ), z( a ), w( a ) {}
			inline vec( T a, T b, T c, T d ) : x( a ), y( b ), z( c ), w( d ) {}

			template <typename O, Uint32 M>
			inline vec( const vec<O, M>& o ) : x( T(o.x) ), y( T(o.y) ), z( T(o.z) ), w( T(o.w) ) {}

			template <typename O, Uint32 M>
			inline vec( const vec<O, M>& o, T d ) : x( T( o.x ) ), y( T( o.y ) ), z( T( o.z ) ), w( d ) {}

			inline T& 		operator[]( Uint32 idx ) { return v[idx]; }
			inline const T 	operator[]( Uint32 idx ) const { return v[idx]; }
		};

	}

	// Type declarations
	using vec2		= internals::vec<Float, 2>;
	using vec3		= internals::vec<Float, 3>;
	using vec4		= internals::vec<Float, 4>;

	using bvec2		= internals::vec<Bool, 2>;
	using bvec3		= internals::vec<Bool, 3>;
	using bvec4		= internals::vec<Bool, 4>;

	using uvec2		= internals::vec<Uint32, 2>;
	using uvec3		= internals::vec<Uint32, 3>;
	using uvec4		= internals::vec<Uint32, 4>;

	using u8vec2	= internals::vec<Uint8, 2>;
	using u8vec3	= internals::vec<Uint8, 3>;
	using u8vec4	= internals::vec<Uint8, 4>;

	using u16vec2	= internals::vec<Uint16, 2>;
	using u16vec3	= internals::vec<Uint16, 3>;
	using u16vec4	= internals::vec<Uint16, 4>;

	using ivec2		= internals::vec<Int32, 2>;
	using ivec3		= internals::vec<Int32, 3>;
	using ivec4		= internals::vec<Int32, 4>;

	using i8vec2	= internals::vec<Int8, 2>;
	using i8vec3	= internals::vec<Int8, 3>;
	using i8vec4	= internals::vec<Int8, 4>;

	using i16vec2	= internals::vec<Int16, 2>;
	using i16vec3	= internals::vec<Int16, 3>;
	using i16vec4	= internals::vec<Int16, 4>;

#define GI_DECLARE_VEC_FUNCTION1(func)																	\
	template <typename T, Uint32 N>																		\
	inline internals::vec<T, N> func( internals::vec<T, N> v )											\
	{																									\
		for( auto& i : v.v ) i = std::func( i );														\
		return v;																						\
	}

#define GI_DECLARE_VEC_FUNCTION2(func)																	\
	template <typename T, Uint32 N>																		\
	inline internals::vec<T, N> func( internals::vec<T, N> a, const internals::vec<T, N>& b )			\
	{																									\
		for( Uint32 i = 0; i < N; ++i ) a[i] = std::func( a[i] , b[i] );								\
		return a;																						\
	}

	GI_DECLARE_VEC_FUNCTION1( abs );
	GI_DECLARE_VEC_FUNCTION1( floor );
	GI_DECLARE_VEC_FUNCTION1( sqrt );
	GI_DECLARE_VEC_FUNCTION2( max );
	GI_DECLARE_VEC_FUNCTION2( min );

	template <typename T, Uint32 N>																	
	inline internals::vec<T, N> clamp(const internals::vec<T, N>& a, const internals::vec<T, N>& b, const internals::vec<T, N>& c )
	{																								
		return max( min( a, c ), b );
	}

#undef GI_DECLARE_VEC_FUNCTION

	template <typename T, Uint32 N>																		
	inline internals::vec<T, N> normalize( const internals::vec<T, N>& a )	
	{																									
		Float sum = 0.0f;
		for( Uint32 i = 0; i < N; ++i ) sum += a[i] * a[i];
		return a * ( 1.0f / Max( std::sqrt( sum ), 1e-6f ) );
	}

#define GI_DECLARE_VEC_BOOL(func,op)																	\
	template <typename T, Uint32 N>																		\
	inline internals::vec<Bool, N> func( const internals::vec<T, N>& a, const internals::vec<T, N>& b )	\
	{																									\
		internals::vec<Bool, N> ret;																	\
		for( Uint32 i = 0; i < N; ++i ) ret[i] = a[i] op b[i];											\
		return ret;																						\
	}

	GI_DECLARE_VEC_BOOL( equal, == );
	GI_DECLARE_VEC_BOOL( notEqual, != );
	GI_DECLARE_VEC_BOOL( lessThan, < );
	GI_DECLARE_VEC_BOOL( greaterThan, > );

#undef GI_DECLARE_VEC_FUNCTION

	template <Uint32 N>
	inline Bool all( const internals::vec<Bool, N>& a )
	{
		Bool ret = true;
		for( auto& i : a.v ) ret = ret && i;
		return ret;
	}

	template <Uint32 N>
	inline Bool any( const internals::vec<Bool, N>& a )
	{
		Bool ret = false;
		for( auto& i : a.v ) ret = ret || i;
		return ret;
	}


	template <typename T, Uint32 N>
	inline T dot( const internals::vec<T, N>& a, const internals::vec<T, N>& b )
	{
		T ret = 0;
		for( Uint32 i = 0; i < N; ++i ) ret += a[i] * b[i];
		return ret;
	}

	template <typename T>
	inline internals::vec<T, 3> cross( const internals::vec<T, 3>& a, const internals::vec<T, 3>& b )
	{
		return internals::vec<T, 3>(	(a.y * b.z) - (a.z * b.y),
										(a.z * b.x) - (a.x * b.z),
										(a.x * b.y) - (a.y * b.x) );
	}

}

//********************************************************************************************//
//********************************************************************************************//
// Global operators

#define GI_DECLARE_VEC_OPERATOR(op)																		\
template <typename T, Uint32 N>																			\
inline mini::internals::vec<T, N> operator op( const mini::internals::vec<T, N>& a, T b )				\
{																										\
	mini::internals::vec<T, N> ret;																		\
	for( Uint32 i = 0; i < N; ++i ) ret[i] = a[i] op b;													\
	return ret;																							\
}																										\
template <typename T, Uint32 N>																			\
inline mini::internals::vec<T, N> operator op( T b, const mini::internals::vec<T, N>& a )				\
{																										\
	mini::internals::vec<T, N> ret;																		\
	for( Uint32 i = 0; i < N; ++i ) ret[i] = b op a[i];													\
	return ret;																							\
}																										\
template <typename T, Uint32 N>																			\
inline mini::internals::vec<T, N> operator op( const mini::internals::vec<T, N>& a, const mini::internals::vec<T, N>& b ) \
{																										\
	mini::internals::vec<T, N> ret;																		\
	for( Uint32 i = 0; i < N; ++i ) ret[i] = a[i] op b[i];												\
	return ret;																							\
}

GI_DECLARE_VEC_OPERATOR( + );
GI_DECLARE_VEC_OPERATOR( - );
GI_DECLARE_VEC_OPERATOR( * );
GI_DECLARE_VEC_OPERATOR( / );

#undef GI_DECLARE_VEC_OPERATOR

//********************************************************************************************//
//********************************************************************************************//
