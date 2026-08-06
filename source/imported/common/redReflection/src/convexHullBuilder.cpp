/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathConvexHull.h"
#include "mathConvexHullEx.h"
#include "convexHullBuilder.h"
#include "../../redCore/include/profilerTypes.h"
#include "../../redSystem/include/stopWatch.h"
#include "../../redContainers/include/dynArray.h"
#include <algorithm>


// Ole Kniemeyer, MAXON Computer GmbH, MIT License
// Convex hull implementation based on Preparata and Hong - O(n log n), numerically stable
namespace ole
{
	// Convex hull computation
	class CHullInternal
	{
	public:
		// 3D float vector
		class PointD
		{
		public:
			Double x;
			Double y;
			Double z;

			PointD( Double x=0, Double y=0, Double z=0 )
				:x(x)
				,y(y)
				,z(z)
			{};

			PointD& setMin( const PointD& a )
			{
				x = math::Min(x,a.x);
				y = math::Min(y,a.y);
				z = math::Min(z,a.z);
				return *this;
			}

			PointD& setMax( const PointD& a )
			{
				x = math::Max(x,a.x);
				y = math::Max(y,a.y);
				z = math::Max(z,a.z);
				return *this;
			}

			PointD operator-( const PointD& a ) const
			{
				return PointD( x - a.x, y-a.y, z-a.z);
			}

			PointD operator+( const PointD& a ) const
			{
				return PointD( x + a.x, y+a.y, z+a.z);
			}

			PointD& operator+=( const PointD& a )
			{
				x += a.x;
				y += a.y;
				z += a.z;
				return *this;
			}

			PointD& operator-=( const PointD& a )
			{
				x -= a.x;
				y -= a.y;
				z -= a.z;
				return *this;
			}

			PointD operator/( Double d ) const
			{
				return PointD( x/d, y/d, z/d );
			}

			PointD operator*( Double d ) const
			{
				return PointD( x*d, y*d, z*d );
			}

			PointD& operator/=( Double d )
			{
				x /= d;
				y /= d;
				z /= d;
				return *this;
			}

			PointD& operator*=( Double d )
			{
				x *= d;
				y *= d;
				z *= d;
				return *this;
			}

			PointD& operator*=( const PointD& d )
			{
				x *= d.x;
				y *= d.y;
				z *= d.z;
				return *this;
			}

			PointD operator*( const PointD& a ) const
			{
				return PointD( x*a.x, y*a.y, z*a.z );
			}

			Uint32 maxAxis() const
			{
				if (x>y && x>z ) return 0;
				if (y>x && y>z ) return 1;
				return 2;
			}

			Uint32 minAxis() const
			{
				if (x<y && x<z ) return 0;
				if (y<x && y<z ) return 1;
				return 2;
			}

			Double& operator[]( Uint32 i )
			{
				return ((Double*)&x)[i];
			}

			const Double& operator[]( Uint32 i ) const 
			{
				return ((const Double*)&x)[i];
			}

			PointD cross( const PointD& v ) const
			{
				return PointD(y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x );
			}

			Double dot( const PointD& v ) const
			{
				return (x*v.x + y*v.y + z*v.z);
			}

			Double length() const
			{
				return sqrt(x*x + y*y + z*z);
			}
		};

		// 64-bit integer point
		class Point64
		{
		public:
			Int64 x;
			Int64 y;
			Int64 z;

			Point64(Int64 x, Int64 y, Int64 z)
				: x(x)
				, y(y)
				, z(z)
			{
			}

			Bool isZero()
			{
				return (x == 0) && (y == 0) && (z == 0);
			}

			Int64 dot(const Point64& b) const
			{
				return x * b.x + y * b.y + z * b.z;
			}
		};

		// 32 bit integer point 
		class Point32
		{
		public:
			Int32 x;
			Int32 y;
			Int32 z;
			Int32 index;

			Point32()
			{
			}

			Point32( Int32 x, Int32 y, Int32 z )
				: x(x)
				, y(y)
				, z(z)
				, index(-1)
			{
			}

			Bool operator==(const Point32& b) const
			{
				return (x == b.x) && (y == b.y) && (z == b.z);
			}

			Bool operator!=(const Point32& b) const
			{
				return (x != b.x) || (y != b.y) || (z != b.z);
			}

			Bool isZero()
			{
				return (x == 0) && (y == 0) && (z == 0);
			}

			Point64 cross(const Point32& b) const
			{
				return Point64(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
			}

			Point64 cross(const Point64& b) const
			{
				return Point64(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
			}

			Int64 dot(const Point32& b) const
			{
				return x * b.x + y * b.y + z * b.z;
			}

			Int64 dot(const Point64& b) const
			{
				return x * b.x + y * b.y + z * b.z;
			}

			Point32 operator+(const Point32& b) const
			{
				return Point32(x + b.x, y + b.y, z + b.z);
			}

			Point32 operator-(const Point32& b) const
			{
				return Point32(x - b.x, y - b.y, z - b.z);
			}
		};

		//! 128 bit integer
		class Int128
		{
		public:
			Uint64 low;
			Uint64 high;

			Int128()
			{
			}

			Int128( Uint64 low, Uint64 high )
				: low(low)
				, high(high)
			{
			}

			Int128( Uint64 low )
				: low(low)
				, high(0)
			{
			}

			Int128(Int64 value)
				: low(value)
				, high((value >= 0) ? 0 : (Uint64) -1LL)
			{
			}

			static Int128 mul(Int64 a, Int64 b);

			static Int128 mul(Uint64 a, Uint64 b);

			Int128 operator-() const
			{
				return Int128((Uint64) -(Int64)low, ~high + (low == 0));
			}

			Int128 operator+(const Int128& b) const
			{
				Uint64 lo = low + b.low;
				return Int128(lo, high + b.high + (lo < low));
			}

			Int128 operator-(const Int128& b) const
			{
				return *this + -b;
			}

			Int128& operator+=(const Int128& b)
			{
				Uint64 lo = low + b.low;
				if (lo < low)
				{
					++high;
				}
				low = lo;
				high += b.high;
				return *this;
			}

			Int128& operator++()
			{
				if (++low == 0)
				{
					++high;
				}
				return *this;
			}

			Int128 operator*(Int64 b) const;

			Double toScalar() const
			{
				return ((Int64) high >= 0) ? float(high) * (Double(0x100000000LL) * Double(0x100000000LL)) + Double(low)
					: -(-*this).toScalar();
			}

			Int32 getSign() const
			{
				return ((Int64) high < 0) ? -1 : (high || low) ? 1 : 0;
			}

			Bool operator<(const Int128& b) const
			{
				return (high < b.high) || ((high == b.high) && (low < b.low));
			}

			Int32 ucmp(const Int128&b) const
			{
				if (high < b.high)
				{
					return -1;
				}
				if (high > b.high)
				{
					return 1;
				}
				if (low < b.low)
				{
					return -1;
				}
				if (low > b.low)
				{
					return 1;
				}
				return 0;
			}
		};

		// Rational number (64 bit num and denom)
		class Rational64
		{
		private:
			Uint64 numerator;
			Uint64 denominator;
			Int32 sign;

		public:
			Rational64( Int64 numerator, Int64 denominator )
			{
				if (numerator > 0)
				{
					sign = 1;
					this->numerator = (Uint64) numerator;
				}
				else if (numerator < 0)
				{
					sign = -1;
					this->numerator = (Uint64) -numerator;
				}
				else
				{
					sign = 0;
					this->numerator = 0;
				}
				if (denominator > 0)
				{
					this->denominator = (Uint64) denominator;
				}
				else if (denominator < 0)
				{
					sign = -sign;
					this->denominator = (Uint64) -denominator;
				}
				else
				{
					this->denominator = 0;
				}
			}

			Bool isNegativeInfinity() const
			{
				return (sign < 0) && (denominator == 0);
			}

			Bool isNaN() const
			{
				return (sign == 0) && (denominator == 0);
			}

			Int32 compare(const Rational64& b) const;

			Double toScalar() const
			{
				return sign * ((denominator == 0) ? DBL_MAX : (Double) numerator / denominator);
			}
		};

		//! 128-bit each rational number
		class Rational128
		{
		private:
			Int128 numerator;
			Int128 denominator;
			Int32 sign;
			Bool isInt64;

		public:
			Rational128(Int64 value)
			{
				if (value > 0)
				{
					sign = 1;
					this->numerator = value;
				}
				else if (value < 0)
				{
					sign = -1;
					this->numerator = -value;
				}
				else
				{
					sign = 0;
					this->numerator = (Uint64) 0;
				}
				this->denominator = (Uint64) 1;
				isInt64 = true;
			}

			Rational128(const Int128& numerator, const Int128& denominator)
			{
				sign = numerator.getSign();
				if (sign >= 0)
				{
					this->numerator = numerator;
				}
				else
				{
					this->numerator = -numerator;
				}
				Int32 dsign = denominator.getSign();
				if (dsign >= 0)
				{
					this->denominator = denominator;
				}
				else
				{
					sign = -sign;
					this->denominator = -denominator;
				}
				isInt64 = false;
			}

			Int32 compare(const Rational128& b) const;

			Int32 compare(Int64 b) const;

			Double toScalar() const
			{
				return sign * ((denominator.getSign() == 0) ? DBL_MAX : numerator.toScalar() / denominator.toScalar());
			}
		};

		/// Rational point
		class PointR128
		{
		public:
			Int128 x;
			Int128 y;
			Int128 z;
			Int128 denominator;

			PointR128()
			{
			}

			PointR128( Int128 x, Int128 y, Int128 z, Int128 denominator )
				: x(x)
				, y(y)
				, z(z)
				, denominator( denominator )
			{
			}

			Double xvalue() const
			{
				return x.toScalar() / denominator.toScalar();
			}

			Double yvalue() const
			{
				return y.toScalar() / denominator.toScalar();
			}

			Double zvalue() const
			{
				return z.toScalar() / denominator.toScalar();
			}
		};

		class Edge;
		class Face;

		class Vertex
		{
		public:
			Vertex* next;
			Vertex* prev;
			Edge* edges;
			Face* firstNearbyFace;
			Face* lastNearbyFace;
			PointR128 point128;
			Point32 point;
			Int32 copy;

			Vertex()
				: next(NULL)
				, prev(NULL)
				, edges(NULL)
				, firstNearbyFace(NULL)
				, lastNearbyFace(NULL)
				, copy(-1)
			{
			}

			Point32 operator-(const Vertex& b) const
			{
				return point - b.point;
			}

			Rational128 dot(const Point64& b) const
			{
				return (point.index >= 0) ? Rational128(point.dot(b))
					: Rational128(point128.x * b.x + point128.y * b.y + point128.z * b.z, point128.denominator);
			}

			Double xvalue() const
			{
				return (point.index >= 0) ? Double(point.x) : point128.xvalue();
			}

			Double yvalue() const
			{
				return (point.index >= 0) ? Double(point.y) : point128.yvalue();
			}

			Double zvalue() const
			{
				return (point.index >= 0) ? Double(point.z) : point128.zvalue();
			}

			void receiveNearbyFaces(Vertex* src)
			{
				if (lastNearbyFace)
				{
					lastNearbyFace->nextWithSameNearbyVertex = src->firstNearbyFace;
				}
				else
				{
					firstNearbyFace = src->firstNearbyFace;
				}
				if (src->lastNearbyFace)
				{
					lastNearbyFace = src->lastNearbyFace;
				}
				for (Face* f = src->firstNearbyFace; f; f = f->nextWithSameNearbyVertex)
				{
					RED_ASSERT(f->nearbyVertex == src);
					f->nearbyVertex = this;
				}
				src->firstNearbyFace = NULL;
				src->lastNearbyFace = NULL;
			}
		};

		class Edge
		{
		public:
			Edge* next;
			Edge* prev;
			Edge* reverse;
			Vertex* target;
			Face* face;
			Int32 copy;

			~Edge()
			{
				next = NULL;
				prev = NULL;
				reverse = NULL;
				target = NULL;
				face = NULL;
			}

			void link(Edge* n)
			{
				RED_ASSERT(reverse->target == n->reverse->target);
				next = n;
				n->prev = this;
			}
		};

		class Face
		{
		public:
			Face* next;
			Vertex* nearbyVertex;
			Face* nextWithSameNearbyVertex;
			Point32 origin;
			Point32 dir0;
			Point32 dir1;

			Face()
				: next(NULL)
				, nearbyVertex(NULL)
				, nextWithSameNearbyVertex(NULL)
			{
			}

			void init(Vertex* a, Vertex* b, Vertex* c)
			{
				nearbyVertex = a;
				origin = a->point;
				dir0 = *b - *a;
				dir1 = *c - *a;
				if (a->lastNearbyFace)
				{
					a->lastNearbyFace->nextWithSameNearbyVertex = this;
				}
				else
				{
					a->firstNearbyFace = this;
				}
				a->lastNearbyFace = this;
			}

			Point64 getNormal()
			{
				return dir0.cross(dir1);
			}
		};

		template<typename UWord, typename UHWord> class DMul
		{
		private:
			static Uint32 high(Uint64 value)
			{
				return (Uint32) (value >> 32);
			}

			static Uint32 low(Uint64 value)
			{
				return (Uint32) value;
			}

			static Uint64 mul(Uint32 a, Uint32 b)
			{
				return (Uint64) a * (Uint64) b;
			}

			static void shlHalf(Uint64& value)
			{
				value <<= 32;
			}

			static Uint64 high(Int128 value)
			{
				return value.high;
			}

			static Uint64 low(Int128 value)
			{
				return value.low;
			}

			static Int128 mul(Uint64 a, Uint64 b)
			{
				return Int128::mul(a, b);
			}

			static void shlHalf(Int128& value)
			{
				value.high = value.low;
				value.low = 0;
			}

		public:
			static void mul(UWord a, UWord b, UWord& resLow, UWord& resHigh)
			{
				UWord p00 = mul(low(a), low(b));
				UWord p01 = mul(low(a), high(b));
				UWord p10 = mul(high(a), low(b));
				UWord p11 = mul(high(a), high(b));
				UWord p0110 = UWord(low(p01)) + UWord(low(p10));
				p11 += high(p01);
				p11 += high(p10);
				p11 += high(p0110);
				shlHalf(p0110);
				p00 += p0110;
				if (p00 < p0110)
				{
					++p11;
				}
				resLow = p00;
				resHigh = p11;
			}
		};

	private:
		class IntermediateHull
		{
		public:
			Vertex* minXy;
			Vertex* maxXy;
			Vertex* minYx;
			Vertex* maxYx;

			IntermediateHull(): minXy(NULL), maxXy(NULL), minYx(NULL), maxYx(NULL)
			{
			}

			void print();
		};

		enum Orientation {NONE, CLOCKWISE, COUNTER_CLOCKWISE};

		template <typename T> class PoolArray
		{
		private:
			T* array;
			Int32 size;

		public:
			PoolArray<T>* next;

			PoolArray(Int32 size): size(size), next(NULL)
			{
				array = (T*) RED_ALLOCATE_ALIGNED( red::PoolEngine, sizeof(T) * size, 16 );
			}

			~PoolArray()
			{
				RED_FREE( red::PoolEngine, array );
			}

			T* init()
			{
				T* o = array;
				for (Int32 i = 0; i < size; i++, o++)
				{
					o->next = (i+1 < size) ? o + 1 : NULL;
				}
				return array;
			}
		};

		template <typename T> class Pool
		{
		private:
			PoolArray<T>* arrays;
			PoolArray<T>* nextArray;
			T* freeObjects;
			Int32 arraySize;

		public:
			Pool(): arrays(NULL), nextArray(NULL), freeObjects(NULL), arraySize(256)
			{
			}

			~Pool()
			{
				while (arrays)
				{
					PoolArray<T>* p = arrays;
					arrays = p->next;
					p->~PoolArray<T>();
					RED_FREE( red::PoolEngine, p );
				}
			}

			void reset()
			{
				nextArray = arrays;
				freeObjects = NULL;
			}

			void setArraySize(Int32 arraySize)
			{
				this->arraySize = arraySize;
			}

			T* newObject()
			{
				T* o = freeObjects;
				if (!o)
				{
					PoolArray<T>* p = nextArray;
					if (p)
					{
						nextArray = p->next;
					}
					else
					{						
						void* memory = RED_ALLOCATE_ALIGNED( red::PoolEngine, sizeof(PoolArray<T>), 16 );
						p = new( memory ) PoolArray<T>(arraySize);
						p->next = arrays;
						arrays = p;
					}
					o = p->init();
				}
				freeObjects = o->next;
				return new(o) T();
			};

			void freeObject(T* object)
			{
				object->~T();
				object->next = freeObjects;
				freeObjects = object;
			}
		};

		PointD scaling;
		PointD center;
		Pool<Vertex> vertexPool;
		Pool<Edge> edgePool;
		Pool<Face> facePool;
		red::DynArray<Vertex*> originalVertices{ red::PoolEngine() };
		Int32 mergeStamp;
		Int32 minAxis;
		Int32 medAxis;
		Int32 maxAxis;
		Int32 usedEdgePairs;
		Int32 maxUsedEdgePairs;

		static Orientation getOrientation(const Edge* prev, const Edge* next, const Point32& s, const Point32& t);
		Edge* findMaxAngle(Bool ccw, const Vertex* start, const Point32& s, const Point64& rxs, const Point64& sxrxs, Rational64& minCot);
		void findEdgeForCoplanarFaces(Vertex* c0, Vertex* c1, Edge*& e0, Edge*& e1, Vertex* stop0, Vertex* stop1);

		Edge* newEdgePair(Vertex* from, Vertex* to);

		void removeEdgePair(Edge* edge)
		{
			Edge* n = edge->next;
			Edge* r = edge->reverse;

			RED_ASSERT(edge->target && r->target);

			if (n != edge)
			{
				n->prev = edge->prev;
				edge->prev->next = n;
				r->target->edges = n;
			}
			else
			{
				r->target->edges = NULL;
			}

			n = r->next;

			if (n != r)
			{
				n->prev = r->prev;
				r->prev->next = n;
				edge->target->edges = n;
			}
			else
			{
				edge->target->edges = NULL;
			}

			edgePool.freeObject(edge);
			edgePool.freeObject(r);
			usedEdgePairs--;
		}

		void computeInternal(Int32 start, Int32 end, IntermediateHull& result);

		Bool mergeProjection(IntermediateHull& h0, IntermediateHull& h1, Vertex*& c0, Vertex*& c1);

		void merge(IntermediateHull& h0, IntermediateHull& h1);

		PointD toBtVector(const Point32& v);

		PointD getBtNormal(Face* face);

		Bool shiftFace(Face* face, Double amount, red::DynArray<Vertex*> stack);

	public:
		Vertex* vertexList;

		void compute(const void* coords, Bool doubleCoords, Int32 stride, Int32 count);

		PointD getCoordinates(const Vertex* v);

		Double shrink(Double amount, Double clampAmount);
	};

	CHullInternal::Int128 CHullInternal::Int128::operator*(Int64 b) const
	{
		Bool negative = (Int64) high < 0;
		Int128 a = negative ? -*this : *this;
		if (b < 0)
		{
			negative = !negative;
			b = -b;
		}
		Int128 result = mul(a.low, (Uint64) b);
		result.high += a.high * (Uint64) b;
		return negative ? -result : result;
	}

	CHullInternal::Int128 CHullInternal::Int128::mul(Int64 a, Int64 b)
	{
		Int128 result;

		Bool negative = a < 0;
		if (negative)
		{
			a = -a;
		}
		if (b < 0)
		{
			negative = !negative;
			b = -b;
		}
		DMul<Uint64, Uint32>::mul((Uint64) a, (Uint64) b, result.low, result.high);
		return negative ? -result : result;
	}

	CHullInternal::Int128 CHullInternal::Int128::mul(Uint64 a, Uint64 b)
	{
		Int128 result;
		DMul<Uint64, Uint32>::mul(a, b, result.low, result.high);
		return result;
	}

	Int32 CHullInternal::Rational64::compare(const Rational64& b) const
	{
		if (sign != b.sign)
		{
			return sign - b.sign;
		}
		else if (sign == 0)
		{
			return 0;
		}

		return sign * Int128::mul(numerator, b.denominator).ucmp(Int128::mul(denominator, b.numerator));
	}

	Int32 CHullInternal::Rational128::compare(const Rational128& b) const
	{
		if (sign != b.sign)
		{
			return sign - b.sign;
		}
		else if (sign == 0)
		{
			return 0;
		}
		if (isInt64)
		{
			return -b.compare(sign * (Int64) numerator.low);
		}

		Int128 nbdLow, nbdHigh, dbnLow, dbnHigh;
		DMul<Int128, Uint64>::mul(numerator, b.denominator, nbdLow, nbdHigh);
		DMul<Int128, Uint64>::mul(denominator, b.numerator, dbnLow, dbnHigh);

		Int32 cmp = nbdHigh.ucmp(dbnHigh);
		if (cmp)
		{
			return cmp * sign;
		}
		return nbdLow.ucmp(dbnLow) * sign;
	}

	Int32 CHullInternal::Rational128::compare(Int64 b) const
	{
		if (isInt64)
		{
			Int64 a = sign * (Int64) numerator.low;
			return (a > b) ? 1 : (a < b) ? -1 : 0;
		}
		if (b > 0)
		{
			if (sign <= 0)
			{
				return -1;
			}
		}
		else if (b < 0)
		{
			if (sign >= 0)
			{
				return 1;
			}
			b = -b;
		}
		else
		{
			return sign;
		}

		return numerator.ucmp(denominator * b) * sign;
	}


	CHullInternal::Edge* CHullInternal::newEdgePair(Vertex* from, Vertex* to)
	{
		RED_ASSERT(from && to);
		Edge* e = edgePool.newObject();
		Edge* r = edgePool.newObject();
		e->reverse = r;
		r->reverse = e;
		e->copy = mergeStamp;
		r->copy = mergeStamp;
		e->target = to;
		r->target = from;
		e->face = NULL;
		r->face = NULL;
		usedEdgePairs++;
		if (usedEdgePairs > maxUsedEdgePairs)
		{
			maxUsedEdgePairs = usedEdgePairs;
		}
		return e;
	}

	Bool CHullInternal::mergeProjection(IntermediateHull& h0, IntermediateHull& h1, Vertex*& c0, Vertex*& c1)
	{
		Vertex* v0 = h0.maxYx;
		Vertex* v1 = h1.minYx;
		if ((v0->point.x == v1->point.x) && (v0->point.y == v1->point.y))
		{
			RED_ASSERT(v0->point.z < v1->point.z);
			Vertex* v1p = v1->prev;
			if (v1p == v1)
			{
				c0 = v0;
				if (v1->edges)
				{
					RED_ASSERT(v1->edges->next == v1->edges);
					v1 = v1->edges->target;
					RED_ASSERT(v1->edges->next == v1->edges);
				}
				c1 = v1;
				return false;
			}
			Vertex* v1n = v1->next;
			v1p->next = v1n;
			v1n->prev = v1p;
			if (v1 == h1.minXy)
			{
				if ((v1n->point.x < v1p->point.x) || ((v1n->point.x == v1p->point.x) && (v1n->point.y < v1p->point.y)))
				{
					h1.minXy = v1n;
				}
				else
				{
					h1.minXy = v1p;
				}
			}
			if (v1 == h1.maxXy)
			{
				if ((v1n->point.x > v1p->point.x) || ((v1n->point.x == v1p->point.x) && (v1n->point.y > v1p->point.y)))
				{
					h1.maxXy = v1n;
				}
				else
				{
					h1.maxXy = v1p;
				}
			}
		}

		v0 = h0.maxXy;
		v1 = h1.maxXy;
		Vertex* v00 = NULL;
		Vertex* v10 = NULL;
		Int32 sign = 1;

		for (Int32 side = 0; side <= 1; side++)
		{		
			Int32 dx = (v1->point.x - v0->point.x) * sign;
			if (dx > 0)
			{
				while (true)
				{
					Int32 dy = v1->point.y - v0->point.y;

					Vertex* w0 = side ? v0->next : v0->prev;
					if (w0 != v0)
					{
						Int32 dx0 = (w0->point.x - v0->point.x) * sign;
						Int32 dy0 = w0->point.y - v0->point.y;
						if ((dy0 <= 0) && ((dx0 == 0) || ((dx0 < 0) && (dy0 * dx <= dy * dx0))))
						{
							v0 = w0;
							dx = (v1->point.x - v0->point.x) * sign;
							continue;
						}
					}

					Vertex* w1 = side ? v1->next : v1->prev;
					if (w1 != v1)
					{
						Int32 dx1 = (w1->point.x - v1->point.x) * sign;
						Int32 dy1 = w1->point.y - v1->point.y;
						Int32 dxn = (w1->point.x - v0->point.x) * sign;
						if ((dxn > 0) && (dy1 < 0) && ((dx1 == 0) || ((dx1 < 0) && (dy1 * dx < dy * dx1))))
						{
							v1 = w1;
							dx = dxn;
							continue;
						}
					}

					break;
				}
			}
			else if (dx < 0)
			{
				while (true)
				{
					Int32 dy = v1->point.y - v0->point.y;

					Vertex* w1 = side ? v1->prev : v1->next;
					if (w1 != v1)
					{
						Int32 dx1 = (w1->point.x - v1->point.x) * sign;
						Int32 dy1 = w1->point.y - v1->point.y;
						if ((dy1 >= 0) && ((dx1 == 0) || ((dx1 < 0) && (dy1 * dx <= dy * dx1))))
						{
							v1 = w1;
							dx = (v1->point.x - v0->point.x) * sign;
							continue;
						}
					}

					Vertex* w0 = side ? v0->prev : v0->next;
					if (w0 != v0)
					{
						Int32 dx0 = (w0->point.x - v0->point.x) * sign;
						Int32 dy0 = w0->point.y - v0->point.y;
						Int32 dxn = (v1->point.x - w0->point.x) * sign;
						if ((dxn < 0) && (dy0 > 0) && ((dx0 == 0) || ((dx0 < 0) && (dy0 * dx < dy * dx0))))
						{
							v0 = w0;
							dx = dxn;
							continue;
						}
					}

					break;
				}
			}
			else
			{
				Int32 x = v0->point.x;
				Int32 y0 = v0->point.y;
				Vertex* w0 = v0;
				Vertex* t;
				while (((t = side ? w0->next : w0->prev) != v0) && (t->point.x == x) && (t->point.y <= y0))
				{
					w0 = t;
					y0 = t->point.y;
				}
				v0 = w0;

				Int32 y1 = v1->point.y;
				Vertex* w1 = v1;
				while (((t = side ? w1->prev : w1->next) != v1) && (t->point.x == x) && (t->point.y >= y1))
				{
					w1 = t;
					y1 = t->point.y;
				}
				v1 = w1;
			}

			if (side == 0)
			{
				v00 = v0;
				v10 = v1;

				v0 = h0.minXy;
				v1 = h1.minXy;
				sign = -1;
			}
		}

		v0->prev = v1;
		v1->next = v0;

		v00->next = v10;
		v10->prev = v00;

		if (h1.minXy->point.x < h0.minXy->point.x)
		{
			h0.minXy = h1.minXy;
		}
		if (h1.maxXy->point.x >= h0.maxXy->point.x)
		{
			h0.maxXy = h1.maxXy;
		}

		h0.maxYx = h1.maxYx;

		c0 = v00;
		c1 = v10;

		return true;
	}

	void CHullInternal::computeInternal(Int32 start, Int32 end, IntermediateHull& result)
	{
		Int32 n = end - start;
		switch (n)
		{
		case 0:
			result.minXy = NULL;
			result.maxXy = NULL;
			result.minYx = NULL;
			result.maxYx = NULL;
			return;
		case 2:
			{
				Vertex* v = originalVertices[start];
				Vertex* w = v + 1;
				if (v->point != w->point)
				{
					Int32 dx = v->point.x - w->point.x;
					Int32 dy = v->point.y - w->point.y;

					if ((dx == 0) && (dy == 0))
					{
						if (v->point.z > w->point.z)
						{
							Vertex* t = w;
							w = v;
							v = t;
						}
						RED_ASSERT(v->point.z < w->point.z);
						v->next = v;
						v->prev = v;
						result.minXy = v;
						result.maxXy = v;
						result.minYx = v;
						result.maxYx = v;
					}
					else
					{
						v->next = w;
						v->prev = w;
						w->next = v;
						w->prev = v;

						if ((dx < 0) || ((dx == 0) && (dy < 0)))
						{
							result.minXy = v;
							result.maxXy = w;
						}
						else
						{
							result.minXy = w;
							result.maxXy = v;
						}

						if ((dy < 0) || ((dy == 0) && (dx < 0)))
						{
							result.minYx = v;
							result.maxYx = w;
						}
						else
						{
							result.minYx = w;
							result.maxYx = v;
						}
					}

					Edge* e = newEdgePair(v, w);
					e->link(e);
					v->edges = e;

					e = e->reverse;
					e->link(e);
					w->edges = e;

					return;
				}
			}
			// lint -fallthrough
		case 1:
			{
				Vertex* v = originalVertices[start];
				v->edges = NULL;
				v->next = v;
				v->prev = v;

				result.minXy = v;
				result.maxXy = v;
				result.minYx = v;
				result.maxYx = v;

				return;
			}
		}

		Int32 split0 = start + n / 2;
		Point32 p = originalVertices[split0-1]->point;
		Int32 split1 = split0;
		while ((split1 < end) && (originalVertices[split1]->point == p))
		{
			split1++;
		}
		computeInternal(start, split0, result);
		IntermediateHull hull1;
		computeInternal(split1, end, hull1);
		merge(result, hull1);
	}

	CHullInternal::Orientation CHullInternal::getOrientation(const Edge* prev, const Edge* next, const Point32& s, const Point32& t)
	{
		RED_ASSERT(prev->reverse->target == next->reverse->target);
		if (prev->next == next)
		{
			if (prev->prev == next)
			{
				Point64 n = t.cross(s);
				Point64 m = (*prev->target - *next->reverse->target).cross(*next->target - *next->reverse->target);
				RED_ASSERT(!m.isZero());
				Int64 dot = n.dot(m);
				RED_ASSERT(dot != 0);
				return (dot > 0) ? COUNTER_CLOCKWISE : CLOCKWISE;
			}
			return COUNTER_CLOCKWISE;
		}
		else if (prev->prev == next)
		{
			return CLOCKWISE;
		}
		else
		{
			return NONE;
		}
	}

	CHullInternal::Edge* CHullInternal::findMaxAngle(Bool ccw, const Vertex* start, const Point32& s, const Point64& rxs, const Point64& sxrxs, Rational64& minCot)
	{
		Edge* minEdge = NULL;

		Edge* e = start->edges;
		if (e)
		{
			do
			{
				if (e->copy > mergeStamp)
				{
					Point32 t = *e->target - *start;
					Rational64 cot(t.dot(sxrxs), t.dot(rxs));
					if (cot.isNaN())
					{
						RED_ASSERT(ccw ? (t.dot(s) < 0) : (t.dot(s) > 0));
					}
					else
					{
						Int32 cmp;
						if (minEdge == NULL)
						{
							minCot = cot;
							minEdge = e;
						}
						else if ((cmp = cot.compare(minCot)) < 0)
						{
							minCot = cot;
							minEdge = e;
						}
						else if ((cmp == 0) && (ccw == (getOrientation(minEdge, e, s, t) == COUNTER_CLOCKWISE)))
						{
							minEdge = e;
						}
					}
				}
				e = e->next;
			} while (e != start->edges);
		}
		return minEdge;
	}

	void CHullInternal::findEdgeForCoplanarFaces(Vertex* c0, Vertex* c1, Edge*& e0, Edge*& e1, Vertex* stop0, Vertex* stop1)
	{
		Edge* start0 = e0;
		Edge* start1 = e1;
		Point32 et0 = start0 ? start0->target->point : c0->point;
		Point32 et1 = start1 ? start1->target->point : c1->point;
		Point32 s = c1->point - c0->point;
		Point64 normal = ((start0 ? start0 : start1)->target->point - c0->point).cross(s);
		Int64 dist = c0->point.dot(normal);
		RED_ASSERT(!start1 || (start1->target->point.dot(normal) == dist));
		Point64 perp = s.cross(normal);
		RED_ASSERT(!perp.isZero());

		Int64 maxDot0 = et0.dot(perp);
		if (e0)
		{
			while (e0->target != stop0)
			{
				Edge* e = e0->reverse->prev;
				if (e->target->point.dot(normal) < dist)
				{
					break;
				}
				RED_ASSERT(e->target->point.dot(normal) == dist);
				if (e->copy == mergeStamp)
				{
					break;
				}
				Int64 dot = e->target->point.dot(perp);
				if (dot <= maxDot0)
				{
					break;
				}
				maxDot0 = dot;
				e0 = e;
				et0 = e->target->point;
			}
		}

		Int64 maxDot1 = et1.dot(perp);
		if (e1)
		{
			while (e1->target != stop1)
			{
				Edge* e = e1->reverse->next;
				if (e->target->point.dot(normal) < dist)
				{
					break;
				}
				RED_ASSERT(e->target->point.dot(normal) == dist);
				if (e->copy == mergeStamp)
				{
					break;
				}
				Int64 dot = e->target->point.dot(perp);
				if (dot <= maxDot1)
				{
					break;
				}
				maxDot1 = dot;
				e1 = e;
				et1 = e->target->point;
			}
		}

		Int64 dx = maxDot1 - maxDot0;
		if (dx > 0)
		{
			while (true)
			{
				Int64 dy = (et1 - et0).dot(s);

				if (e0 && (e0->target != stop0))
				{
					Edge* f0 = e0->next->reverse;
					if (f0->copy > mergeStamp)
					{
						Int64 dx0 = (f0->target->point - et0).dot(perp);
						Int64 dy0 = (f0->target->point - et0).dot(s);
						if ((dx0 == 0) ? (dy0 < 0) : ((dx0 < 0) && (Rational64(dy0, dx0).compare(Rational64(dy, dx)) >= 0)))
						{
							et0 = f0->target->point;
							dx = (et1 - et0).dot(perp);
							e0 = (e0 == start0) ? NULL : f0;
							continue;
						}
					}
				}

				if (e1 && (e1->target != stop1))
				{
					Edge* f1 = e1->reverse->next;
					if (f1->copy > mergeStamp)
					{
						Point32 d1 = f1->target->point - et1;
						if (d1.dot(normal) == 0)
						{
							Int64 dx1 = d1.dot(perp);
							Int64 dy1 = d1.dot(s);
							Int64 dxn = (f1->target->point - et0).dot(perp);
							if ((dxn > 0) && ((dx1 == 0) ? (dy1 < 0) : ((dx1 < 0) && (Rational64(dy1, dx1).compare(Rational64(dy, dx)) > 0))))
							{
								e1 = f1;
								et1 = e1->target->point;
								dx = dxn;
								continue;
							}
						}
						else
						{
							RED_ASSERT((e1 == start1) && (d1.dot(normal) < 0));
						}
					}
				}

				break;
			}
		}
		else if (dx < 0)
		{
			while (true)
			{
				Int64 dy = (et1 - et0).dot(s);

				if (e1 && (e1->target != stop1))
				{
					Edge* f1 = e1->prev->reverse;
					if (f1->copy > mergeStamp)
					{
						Int64 dx1 = (f1->target->point - et1).dot(perp);
						Int64 dy1 = (f1->target->point - et1).dot(s);
						if ((dx1 == 0) ? (dy1 > 0) : ((dx1 < 0) && (Rational64(dy1, dx1).compare(Rational64(dy, dx)) <= 0)))
						{
							et1 = f1->target->point;
							dx = (et1 - et0).dot(perp);
							e1 = (e1 == start1) ? NULL : f1;
							continue;
						}
					}
				}

				if (e0 && (e0->target != stop0))
				{
					Edge* f0 = e0->reverse->prev;
					if (f0->copy > mergeStamp)
					{
						Point32 d0 = f0->target->point - et0;
						if (d0.dot(normal) == 0)
						{
							Int64 dx0 = d0.dot(perp);
							Int64 dy0 = d0.dot(s);
							Int64 dxn = (et1 - f0->target->point).dot(perp);
							if ((dxn < 0) && ((dx0 == 0) ? (dy0 > 0) : ((dx0 < 0) && (Rational64(dy0, dx0).compare(Rational64(dy, dx)) < 0))))
							{
								e0 = f0;
								et0 = e0->target->point;
								dx = dxn;
								continue;
							}
						}
						else
						{
							RED_ASSERT((e0 == start0) && (d0.dot(normal) < 0));
						}
					}
				}

				break;
			}
		}
	}

	void CHullInternal::merge(IntermediateHull& h0, IntermediateHull& h1)
	{
		if (!h1.maxXy)
		{
			return;
		}
		if (!h0.maxXy)
		{
			h0 = h1;
			return;
		}

		mergeStamp--;

		Vertex* c0 = NULL;
		Edge* toPrev0 = NULL;
		Edge* firstNew0 = NULL;
		Edge* pendingHead0 = NULL;
		Edge* pendingTail0 = NULL;
		Vertex* c1 = NULL;
		Edge* toPrev1 = NULL;
		Edge* firstNew1 = NULL;
		Edge* pendingHead1 = NULL;
		Edge* pendingTail1 = NULL;
		Point32 prevPoint;

		if (mergeProjection(h0, h1, c0, c1))
		{
			Point32 s = *c1 - *c0;
			Point64 normal = Point32(0, 0, -1).cross(s);
			Point64 t = s.cross(normal);
			RED_ASSERT(!t.isZero());

			Edge* e = c0->edges;
			Edge* start0 = NULL;
			if (e)
			{
				do
				{
					Int64 dot = (*e->target - *c0).dot(normal);
					RED_ASSERT(dot <= 0);
					if ((dot == 0) && ((*e->target - *c0).dot(t) > 0))
					{
						if (!start0 || (getOrientation(start0, e, s, Point32(0, 0, -1)) == CLOCKWISE))
						{
							start0 = e;
						}
					}
					e = e->next;
				} while (e != c0->edges);
			}

			e = c1->edges;
			Edge* start1 = NULL;
			if (e)
			{
				do
				{
					Int64 dot = (*e->target - *c1).dot(normal);
					RED_ASSERT(dot <= 0);
					if ((dot == 0) && ((*e->target - *c1).dot(t) > 0))
					{
						if (!start1 || (getOrientation(start1, e, s, Point32(0, 0, -1)) == COUNTER_CLOCKWISE))
						{
							start1 = e;
						}
					}
					e = e->next;
				} while (e != c1->edges);
			}

			if (start0 || start1)
			{
				findEdgeForCoplanarFaces(c0, c1, start0, start1, NULL, NULL);
				if (start0)
				{
					c0 = start0->target;
				}
				if (start1)
				{
					c1 = start1->target;
				}
			}

			prevPoint = c1->point;
			prevPoint.z++;
		}
		else
		{
			prevPoint = c1->point;
			prevPoint.x++;
		}

		Vertex* first0 = c0;
		Vertex* first1 = c1;
		Bool firstRun = true;

		while (true)
		{
			Point32 s = *c1 - *c0;
			Point32 r = prevPoint - c0->point;
			Point64 rxs = r.cross(s);
			Point64 sxrxs = s.cross(rxs);

			Rational64 minCot0(0, 0);
			Edge* min0 = findMaxAngle(false, c0, s, rxs, sxrxs, minCot0);
			Rational64 minCot1(0, 0);
			Edge* min1 = findMaxAngle(true, c1, s, rxs, sxrxs, minCot1);
			if (!min0 && !min1)
			{
				Edge* e = newEdgePair(c0, c1);
				e->link(e);
				c0->edges = e;

				e = e->reverse;
				e->link(e);
				c1->edges = e;
				return;
			}
			else
			{
				Int32 cmp = !min0 ? 1 : !min1 ? -1 : minCot0.compare(minCot1);
				if (firstRun || ((cmp >= 0) ? !minCot1.isNegativeInfinity() : !minCot0.isNegativeInfinity()))
				{
					Edge* e = newEdgePair(c0, c1);
					if (pendingTail0)
					{
						pendingTail0->prev = e;
					}
					else
					{
						pendingHead0 = e;
					}
					e->next = pendingTail0;
					pendingTail0 = e;

					e = e->reverse;
					if (pendingTail1)
					{
						pendingTail1->next = e;
					}
					else
					{
						pendingHead1 = e;
					}
					e->prev = pendingTail1;
					pendingTail1 = e;
				}

				Edge* e0 = min0;
				Edge* e1 = min1;

				if (cmp == 0)
				{
					findEdgeForCoplanarFaces(c0, c1, e0, e1, NULL, NULL);
				}

				if ((cmp >= 0) && e1)
				{
					if (toPrev1)
					{
						for (Edge* e = toPrev1->next, *n = NULL; e != min1; e = n)
						{
							n = e->next;
							removeEdgePair(e);
						}
					}

					if (pendingTail1)
					{
						if (toPrev1)
						{
							toPrev1->link(pendingHead1);
						}
						else
						{
							min1->prev->link(pendingHead1);
							firstNew1 = pendingHead1;
						}
						pendingTail1->link(min1);
						pendingHead1 = NULL;
						pendingTail1 = NULL;
					}
					else if (!toPrev1)
					{
						firstNew1 = min1;
					}

					prevPoint = c1->point;
					c1 = e1->target;
					toPrev1 = e1->reverse;
				}

				if ((cmp <= 0) && e0)
				{
					if (toPrev0)
					{
						for (Edge* e = toPrev0->prev, *n = NULL; e != min0; e = n)
						{
							n = e->prev;
							removeEdgePair(e);
						}
					}

					if (pendingTail0)
					{
						if (toPrev0)
						{
							pendingHead0->link(toPrev0);
						}
						else
						{
							pendingHead0->link(min0->next);
							firstNew0 = pendingHead0;
						}
						min0->link(pendingTail0);
						pendingHead0 = NULL;
						pendingTail0 = NULL;
					}
					else if (!toPrev0)
					{
						firstNew0 = min0;
					}

					prevPoint = c0->point;
					c0 = e0->target;
					toPrev0 = e0->reverse;
				}
			}

			if ((c0 == first0) && (c1 == first1))
			{
				if (toPrev0 == NULL)
				{
					pendingHead0->link(pendingTail0);
					c0->edges = pendingTail0;
				}
				else
				{
					for (Edge* e = toPrev0->prev, *n = NULL; e != firstNew0; e = n)
					{
						n = e->prev;
						removeEdgePair(e);
					}
					if (pendingTail0)
					{
						pendingHead0->link(toPrev0);
						firstNew0->link(pendingTail0);
					}
				}

				if (toPrev1 == NULL)
				{
					pendingTail1->link(pendingHead1);
					c1->edges = pendingTail1;
				}
				else
				{
					for (Edge* e = toPrev1->next, *n = NULL; e != firstNew1; e = n)
					{
						n = e->next;
						removeEdgePair(e);
					}
					if (pendingTail1)
					{
						toPrev1->link(pendingHead1);
						pendingTail1->link(firstNew1);
					}
				}

				return;
			}

			firstRun = false;
		}
	}


	static Bool pointCmp(const CHullInternal::Point32& p, const CHullInternal::Point32& q)
	{
		return (p.y < q.y) || ((p.y == q.y) && ((p.x < q.x) || ((p.x == q.x) && (p.z < q.z))));
	}

	void CHullInternal::compute(const void* coords, Bool doubleCoords, Int32 stride, Int32 count)
	{
		PointD vmin(Double(1e30), Double(1e30), Double(1e30));
		PointD vmax(Double(-1e30), Double(-1e30), Double(-1e30));
		const char* ptr = (const char*) coords;
		if (doubleCoords)
		{
			for (Int32 i = 0; i < count; i++)
			{
				const Double* v = (const Double*) ptr;
				PointD p((Double) v[0], (Double) v[1], (Double) v[2]);
				ptr += stride;
				vmin.setMin(p);
				vmax.setMax(p);
			}
		}
		else
		{
			for (Int32 i = 0; i < count; i++)
			{
				const float* v = (const float*) ptr;
				PointD p(v[0], v[1], v[2]);
				ptr += stride;
				vmin.setMin(p);
				vmax.setMax(p);
			}
		}

		PointD s = vmax - vmin;
		maxAxis = s.maxAxis();
		minAxis = s.minAxis();
		if (minAxis == maxAxis)
		{
			minAxis = (maxAxis + 1) % 3;
		}
		medAxis = 3 - maxAxis - minAxis;

		s = s / Double(10216);

		scaling = s;
		if (s.x > 0)
		{
			s.x = Double(1) / s.x;
		}
		if (s.y > 0)
		{
			s.y = Double(1) / s.y;
		}
		if (s.z > 0)
		{
			s.z = Double(1) / s.z;
		}

		center = (vmin + vmax) * Double(0.5);

		red::DynArray<Point32> points{ red::PoolEngine() };
		points.Resize(count);
		ptr = (const char*) coords;
		if (doubleCoords)
		{
			for (Int32 i = 0; i < count; i++)
			{
				const Double* v = (const Double*) ptr;
				PointD p((Double) v[0], (Double) v[1], (Double) v[2]);
				ptr += stride;
				p = (p - center) * s;
				points[i].x = (Int32) p[medAxis];
				points[i].y = (Int32) p[maxAxis];
				points[i].z = (Int32) p[minAxis];
				points[i].index = i;
			}
		}
		else
		{
			for (Int32 i = 0; i < count; i++)
			{
				const float* v = (const float*) ptr;
				PointD p(v[0], v[1], v[2]);
				ptr += stride;
				p = (p - center) * s;
				points[i].x = (Int32) p[medAxis];
				points[i].y = (Int32) p[maxAxis];
				points[i].z = (Int32) p[minAxis];
				points[i].index = i;
			}
		}
		std::sort( points.Begin(), points.End(), pointCmp );
		//points.quickSort(pointCmp);

		vertexPool.reset();
		vertexPool.setArraySize(count);
		originalVertices.Resize(count);
		for (Int32 i = 0; i < count; i++)
		{
			Vertex* v = vertexPool.newObject();
			v->edges = NULL;
			v->point = points[i];
			v->copy = -1;
			originalVertices[i] = v;
		}

		points.Clear();

		edgePool.reset();
		edgePool.setArraySize(6 * count);

		usedEdgePairs = 0;
		maxUsedEdgePairs = 0;

		mergeStamp = -3;

		IntermediateHull hull;
		computeInternal(0, count, hull);
		vertexList = hull.minXy;
	}

	CHullInternal::PointD CHullInternal::toBtVector(const Point32& v)
	{
		PointD p;
		p[medAxis] = Double(v.x);
		p[maxAxis] = Double(v.y);
		p[minAxis] = Double(v.z);
		return p * scaling;
	}

	CHullInternal::PointD CHullInternal::getBtNormal(Face* face)
	{
		PointD normal = toBtVector(face->dir0).cross(toBtVector(face->dir1));
		normal /= ((medAxis + 1 == maxAxis) || (medAxis - 2 == maxAxis)) ? normal.length() : -normal.length();
		return normal;
	}

	CHullInternal::PointD CHullInternal::getCoordinates(const Vertex* v)
	{
		PointD p;
		p[medAxis] = v->xvalue();
		p[maxAxis] = v->yvalue();
		p[minAxis] = v->zvalue();
		return p * scaling + center;
	}

	Double CHullInternal::shrink(Double amount, Double clampAmount)
	{
		if (!vertexList)
		{
			return 0;
		}
		Int32 stamp = --mergeStamp;
		red::DynArray<Vertex*> stack{ red::PoolEngine() };
		vertexList->copy = stamp;
		stack.PushBack(vertexList);
		red::DynArray<Face*> faces{ red::PoolEngine() };

		Point32 ref = vertexList->point;
		Int128 hullCenterX(0, 0);
		Int128 hullCenterY(0, 0);
		Int128 hullCenterZ(0, 0);
		Int128 volume(0, 0);

		while (stack.Size() > 0)
		{
			Vertex* v = stack[stack.Size() - 1];
			stack.PopBack();
			Edge* e = v->edges;
			if (e)
			{
				do
				{
					if (e->target->copy != stamp)
					{
						e->target->copy = stamp;
						stack.PushBack(e->target);
					}
					if (e->copy != stamp)
					{
						Face* face = facePool.newObject();
						face->init(e->target, e->reverse->prev->target, v);
						faces.PushBack(face);
						Edge* f = e;

						Vertex* a = NULL;
						Vertex* b = NULL;
						do
						{
							if (a && b)
							{
								Int64 vol = (v->point - ref).dot((a->point - ref).cross(b->point - ref));
								RED_ASSERT(vol >= 0);
								Point32 c = v->point + a->point + b->point + ref;
								hullCenterX += vol * c.x;
								hullCenterY += vol * c.y;
								hullCenterZ += vol * c.z;
								volume += vol;
							}

							RED_ASSERT(f->copy != stamp);
							f->copy = stamp;
							f->face = face;

							a = b;
							b = f->target;

							f = f->reverse->prev;
						} while (f != e);
					}
					e = e->next;
				} while (e != v->edges);
			}
		}

		if (volume.getSign() <= 0)
		{
			return 0;
		}

		PointD hullCenter;
		hullCenter[medAxis] = hullCenterX.toScalar();
		hullCenter[maxAxis] = hullCenterY.toScalar();
		hullCenter[minAxis] = hullCenterZ.toScalar();
		hullCenter /= 4 * volume.toScalar();
		hullCenter *= scaling;

		const auto faceCount = faces.Size();
		if (clampAmount > 0)
		{
			Double minDist = DBL_MAX;
			for (Uint32 i = 0; i < faceCount; i++)
			{
				PointD normal = getBtNormal(faces[i]);
				Double dist = normal.dot(toBtVector(faces[i]->origin) - hullCenter);
				if (dist < minDist)
				{
					minDist = dist;
				}
			}

			if (minDist <= 0)
			{
				return 0;
			}

			amount = math::Min(amount, minDist * clampAmount);
		}

		Uint32 seed = 243703;
		for (Uint32 i = 0; i < faceCount; i++, seed = 1664525 * seed + 1013904223)
		{
			Face* x = faces[i];
			faces[i] = faces[seed % faceCount];
			faces[seed % faceCount] = x;
			//btSwap(faces[i], faces[seed % faceCount]);
		}

		for (Uint32 i = 0; i < faceCount; i++)
		{
			if (!shiftFace(faces[i], amount, stack))
			{
				return -amount;
			}
		}

		return amount;
	}

	Bool CHullInternal::shiftFace(Face* face, Double amount, red::DynArray<Vertex*> stack)
	{
		PointD origShift = getBtNormal(face) * -amount;
		if (scaling[0] > 0)
		{
			origShift[0] /= scaling[0];
		}
		if (scaling[1] > 0)
		{
			origShift[1] /= scaling[1];
		}
		if (scaling[2] > 0)
		{
			origShift[2] /= scaling[2];
		}
		Point32 shift((Int32) origShift[medAxis], (Int32) origShift[maxAxis], (Int32) origShift[minAxis]);
		if (shift.isZero())
		{
			return true;
		}
		Point64 normal = face->getNormal();
		Int64 origDot = face->origin.dot(normal);
		Point32 shiftedOrigin = face->origin + shift;
		Int64 shiftedDot = shiftedOrigin.dot(normal);
		RED_ASSERT(shiftedDot <= origDot);
		if (shiftedDot >= origDot)
		{
			return false;
		}

		Edge* intersection = NULL;

		Edge* startEdge = face->nearbyVertex->edges;
		Rational128 optDot = face->nearbyVertex->dot(normal);
		Int32 cmp = optDot.compare(shiftedDot);
		if (cmp >= 0)
		{
			Edge* e = startEdge;
			do
			{
				Rational128 dot = e->target->dot(normal);
				RED_ASSERT(dot.compare(origDot) <= 0);
				if (dot.compare(optDot) < 0)
				{
					Int32 c = dot.compare(shiftedDot);
					optDot = dot;
					e = e->reverse;
					startEdge = e;
					if (c < 0)
					{
						intersection = e;
						break;
					}
					cmp = c;
				}
				e = e->prev;
			} while (e != startEdge);

			if (!intersection)
			{
				return false;
			}
		}
		else
		{
			Edge* e = startEdge;
			do
			{
				Rational128 dot = e->target->dot(normal);
				RED_ASSERT(dot.compare(origDot) <= 0);
				if (dot.compare(optDot) > 0)
				{
					cmp = dot.compare(shiftedDot);
					if (cmp >= 0)
					{
						intersection = e;
						break;
					}
					optDot = dot;
					e = e->reverse;
					startEdge = e;
				}
				e = e->prev;
			} while (e != startEdge);

			if (!intersection)
			{
				return true;
			}
		}

		if (cmp == 0)
		{
			Edge* e = intersection->reverse->next;
			while (e->target->dot(normal).compare(shiftedDot) <= 0)
			{
				e = e->next;
				if (e == intersection->reverse)
				{
					return true;
				}
			}
		}

		Edge* firstIntersection = NULL;
		Edge* faceEdge = NULL;
		Edge* firstFaceEdge = NULL;

		while (true)
		{
			if (cmp == 0)
			{
				Edge* e = intersection->reverse->next;
				startEdge = e;
				while (true)
				{
					if (e->target->dot(normal).compare(shiftedDot) >= 0)
					{
						break;
					}
					intersection = e->reverse;
					e = e->next;
					if (e == startEdge)
					{
						return true;
					}
				}
			}

			if (!firstIntersection)
			{
				firstIntersection = intersection;
			}
			else if (intersection == firstIntersection)
			{
				break;
			}

			Int32 prevCmp = cmp;
			Edge* prevIntersection = intersection;
			Edge* prevFaceEdge = faceEdge;

			Edge* e = intersection->reverse;
			while (true)
			{
				e = e->reverse->prev;
				RED_ASSERT(e != intersection->reverse);
				cmp = e->target->dot(normal).compare(shiftedDot);
				if (cmp >= 0)
				{
					intersection = e;
					break;
				}
			}

			if (cmp > 0)
			{
				Vertex* removed = intersection->target;
				e = intersection->reverse;
				if (e->prev == e)
				{
					removed->edges = NULL;
				}
				else
				{
					removed->edges = e->prev;
					e->prev->link(e->next);
					e->link(e);
				}

				Point64 n0 = intersection->face->getNormal();
				Point64 n1 = intersection->reverse->face->getNormal();
				Int64 m00 = face->dir0.dot(n0);
				Int64 m01 = face->dir1.dot(n0);
				Int64 m10 = face->dir0.dot(n1);
				Int64 m11 = face->dir1.dot(n1);
				Int64 r0 = (intersection->face->origin - shiftedOrigin).dot(n0);
				Int64 r1 = (intersection->reverse->face->origin - shiftedOrigin).dot(n1);
				Int128 det = Int128::mul(m00, m11) - Int128::mul(m01, m10);
				RED_ASSERT(det.getSign() != 0);
				Vertex* v = vertexPool.newObject();
				v->point.index = -1;
				v->copy = -1;
				v->point128 = PointR128(Int128::mul(face->dir0.x * r0, m11) - Int128::mul(face->dir0.x * r1, m01)
					+ Int128::mul(face->dir1.x * r1, m00) - Int128::mul(face->dir1.x * r0, m10) + det * shiftedOrigin.x,
					Int128::mul(face->dir0.y * r0, m11) - Int128::mul(face->dir0.y * r1, m01)
					+ Int128::mul(face->dir1.y * r1, m00) - Int128::mul(face->dir1.y * r0, m10) + det * shiftedOrigin.y,
					Int128::mul(face->dir0.z * r0, m11) - Int128::mul(face->dir0.z * r1, m01)
					+ Int128::mul(face->dir1.z * r1, m00) - Int128::mul(face->dir1.z * r0, m10) + det * shiftedOrigin.z,
					det);
				v->point.x = (Int32) v->point128.xvalue();
				v->point.y = (Int32) v->point128.yvalue();
				v->point.z = (Int32) v->point128.zvalue();
				intersection->target = v;
				v->edges = e;

				stack.PushBack(v);
				stack.PushBack(removed);
				stack.PushBack(NULL);
			}

			if (cmp || prevCmp || (prevIntersection->reverse->next->target != intersection->target))
			{
				faceEdge = newEdgePair(prevIntersection->target, intersection->target);
				if (prevCmp == 0)
				{
					faceEdge->link(prevIntersection->reverse->next);
				}
				if ((prevCmp == 0) || prevFaceEdge)
				{
					prevIntersection->reverse->link(faceEdge);
				}
				if (cmp == 0)
				{
					intersection->reverse->prev->link(faceEdge->reverse);
				}
				faceEdge->reverse->link(intersection->reverse);
			}
			else
			{
				faceEdge = prevIntersection->reverse->next;
			}

			if (prevFaceEdge)
			{
				if (prevCmp > 0)
				{
					faceEdge->link(prevFaceEdge->reverse);
				}
				else if (faceEdge != prevFaceEdge->reverse)
				{
					stack.PushBack(prevFaceEdge->target);
					while (faceEdge->next != prevFaceEdge->reverse)
					{
						Vertex* removed = faceEdge->next->target;
						removeEdgePair(faceEdge->next);
						stack.PushBack(removed);
					}
					stack.PushBack(NULL);
				}
			}
			faceEdge->face = face;
			faceEdge->reverse->face = intersection->face;

			if (!firstFaceEdge)
			{
				firstFaceEdge = faceEdge;
			}
		}

		if (cmp > 0)
		{
			firstFaceEdge->reverse->target = faceEdge->target;
			firstIntersection->reverse->link(firstFaceEdge);
			firstFaceEdge->link(faceEdge->reverse);
		}
		else if (firstFaceEdge != faceEdge->reverse)
		{
			stack.PushBack(faceEdge->target);
			while (firstFaceEdge->next != faceEdge->reverse)
			{
				Vertex* removed = firstFaceEdge->next->target;
				removeEdgePair(firstFaceEdge->next);
				stack.PushBack(removed);
			}
			stack.PushBack(NULL);
		}

		RED_ASSERT(stack.Size() > 0);
		vertexList = stack[0];

		Int32 pos = 0;
		while (pos < (Int32)stack.Size())
		{
			const Int32 end = stack.Size();
			while (pos < end)
			{
				Vertex* kept = stack[pos++];
				Bool deeper = false;
				Vertex* removed;
				while ((removed = stack[pos++]) != NULL)
				{
					kept->receiveNearbyFaces(removed);
					while (removed->edges)
					{
						if (!deeper)
						{
							deeper = true;
							stack.PushBack(kept);
						}
						stack.PushBack(removed->edges->target);
						removeEdgePair(removed->edges);
					}
				}
				if (deeper)
				{
					stack.PushBack(NULL);
				}
			}
		}

		stack.Resize(0);
		face->origin = shiftedOrigin;

		return true;
	}
}


namespace red
{

	//-----

	ConvexBuilder::ConvexBuilder()
	{
		// reserve some space in the arrays
		m_planes.Reserve( 256 );
		m_vertices.Reserve( 1024 );
		m_faces.Reserve( 256 );
		m_edges.Reserve( 1024 );
	}

	ConvexBuilder::~ConvexBuilder()
	{
	}


	static Int32 GetVertexCopy( ole::CHullInternal::Vertex* vertex, red::DynArray<ole::CHullInternal::Vertex*>& vertices)
	{
		Int32 index = vertex->copy;
		if (index < 0)
		{
			index = static_cast< Int32 >( vertices.Size() );
			vertex->copy = index;
			vertices.PushBack(vertex);
		}
		return index;
	}

	Bool ConvexBuilder::Build( const Vector3* vertices, const Uint32 vertexCount )
	{
		// Cleanup - prepare for next convex being built
		m_planes.Clear();
		m_vertices.Clear();
		m_faces.Clear();
		m_edges.Clear();

		// doomed to fail
		if ( vertexCount < 4 )
			return false;

		// Compute convex hull
		ole::CHullInternal hull;
		hull.compute( vertices, false, sizeof(Vector3), vertexCount );

		red::DynArray< ole::CHullInternal::Vertex* > oldVertices{ red::PoolEngine() };
		GetVertexCopy(hull.vertexList, oldVertices);

		Int32 copied = 0;
		while ( copied < (Int32)oldVertices.Size() )
		{
			const auto* v = oldVertices[copied];

			// extract vertex
			{
				auto point = hull.getCoordinates(v);
				m_vertices.PushBack( Vector3( (Float)point.x, (Float)point.y, (Float)point.z ) );
			}

			// extract edges from vertex
			auto* firstEdge = v->edges;
			if ( firstEdge )
			{
				Int32 firstCopy = -1;
				Int32 prevCopy = -1;
				auto* e = firstEdge;
				do
				{
					if ( e->copy < 0 )
					{
						const Uint32 edgeIndex = m_edges.Size();
						m_edges.PushBack( Edge() );
						m_edges.PushBack( Edge() );
						auto& c = m_edges[ edgeIndex ];
						auto& r = m_edges[ edgeIndex + 1 ];

						e->copy = edgeIndex;
						e->reverse->copy = edgeIndex + 1;

						c.m_reverse = 1;
						r.m_reverse = -1;
						c.m_targetVertex = GetVertexCopy(e->target, oldVertices);
						r.m_targetVertex = copied;
					}

					// link edges
					if (prevCopy >= 0)
					{
						m_edges[e->copy].m_next = prevCopy;
					}
					else
					{
						firstCopy = e->copy;
					}

					prevCopy = e->copy;
					e = e->next;
				}
				while (e != firstEdge);

				m_edges[firstCopy].m_next = prevCopy;
			}
			copied++;
		}

		// extract faces
		for ( Int32 i = 0; i < copied; i++ )
		{
			const auto* v = oldVertices[i];
			auto* firstEdge = v->edges;
			if ( firstEdge )
			{
				auto* e = firstEdge;
				do
				{
					if (e->copy >= 0)
					{
						m_faces.PushBack( e->copy ); // first edge in face

						// unlink other faces
						auto* f = e;
						do
						{
							f->copy = -1;
							f = f->reverse->prev;
						}
						while (f != e);
					}
					e = e->next;
				}
				while (e != firstEdge);
			}
		}

		// compute plane equations
		for ( const auto& firstFaceEdge : m_faces )
		{
			const auto& edgeA = m_edges[ firstFaceEdge ];
			const auto& a = m_vertices[ edgeA.m_targetVertex ];

			const auto& edgeB = m_edges[ edgeA.m_next ];
			const auto& b = m_vertices[ edgeB.m_targetVertex ];

			const auto& edgeC = m_edges[ edgeB.m_next ];
			const auto& c = m_vertices[ edgeC.m_targetVertex ];

			m_planes.PushBack( Plane( a, b, c ) );
		}

		return true;
	}

	Bool ConvexBuilder::Extract( struct ConvexHull& outHull ) const
	{
		RED_ASSERT( IsValid(), "Calling extract on convex hull builder with no hull" );

		// extract just the planes
		outHull.m_planes.Reserve( m_planes.Size() );
		for ( const auto& srcPlane : m_planes )
		{
			outHull.m_planes.PushBack( srcPlane.NormalDistance );
		}

		// valid convex extracted
		return true;
	}

	template< typename T >
	static T* AllocPtr( void* base, void*& ptr, const Uint32 count=1, Uint16* outOffset = nullptr )
	{
		// store offset
		if ( outOffset )
		{
			const Uint32 offset = (Uint32)( (const Uint8*) ptr - (const Uint8*) base );
			RED_FATAL_ASSERT( offset < 65535, "Invalid convex data offset: %d", offset );
			*outOffset = (Uint16) offset;
		}

		// get the write ptr
		T* writePtr = (T*) ptr;

		// advance
		ptr = OffsetPtr( ptr, count * sizeof(T) );
		return writePtr;
	}

	Bool ConvexBuilder::ExtractEx( struct ConvexHullEx& outHull ) const
	{
		if( !IsValid() )
		{
			RED_LOG_WARNING("Not enough faces to generate convex hull");
			return false;
		}

		// count memory needed
		Uint32 memoryNeeded = sizeof( red::ConvexHullEx::Header );
		memoryNeeded += sizeof( red::ConvexHullEx::Vertex ) * m_vertices.Size();
		memoryNeeded += sizeof( red::ConvexHullEx::Edge ) * m_edges.Size();
		memoryNeeded += sizeof( red::ConvexHullEx::Face ) * m_faces.Size();
		memoryNeeded += sizeof( Plane ) * m_planes.Size();

		// data is to big ?
		if ( memoryNeeded > 65535 )
		{
			RED_LOG_ERROR( "Core: Convex requires more than 64KB of data (%1.2f KB), to complex to extract.", memoryNeeded / 1024.0f );
			return false;
		}

		// allocate memory
		DataBuffer data( memoryNeeded );

		// setup header
		void* basePtr = data.Data();
		void* writePtr = data.Data();
		auto* header = AllocPtr< red::ConvexHullEx::Header >( basePtr, writePtr );		

		// setup planes
		{
			header->m_numPlanes = (Uint16) m_planes.Size();
			auto* planes = AllocPtr< Plane >( basePtr, writePtr, m_planes.Size(), &header->m_planeOffset );
			red::Memcpy( planes, m_planes.Data(), m_planes.DataSize() );
		}

		// setup vertices
		{
			header->m_numVertices = (Uint16) m_vertices.Size();
			auto* vertices = AllocPtr< red::ConvexHullEx::Vertex >( basePtr, writePtr, m_vertices.Size(), &header->m_vertexOffset );
			red::Memcpy( vertices, m_vertices.Data(), m_vertices.DataSize() );
		}

		// setup faces
		{
			header->m_numFaces = (Uint16) m_faces.Size();
			auto* faces = AllocPtr< red::ConvexHullEx::Face >( basePtr, writePtr, m_faces.Size(), &header->m_faceOffset );

			for ( Uint32 i=0; i<m_faces.Size(); ++i )
				faces[i] = (Uint16) m_faces[i];
		}

		// setup edges
		{
			header->m_numEdges = (Uint16) m_vertices.Size();
			auto* edges = AllocPtr< red::ConvexHullEx::Edge >( basePtr, writePtr, m_edges.Size(), &header->m_edgeOffset );

			for ( Uint32 i=0; i<m_edges.Size(); ++i )
			{
				const auto& srcEdge = m_edges[i];
				edges[i].m_next = (Int16)srcEdge.m_next - (Int16)i;
				edges[i].m_reverse = (Int16)srcEdge.m_reverse;
				edges[i].m_targetVertex = (Int16)srcEdge.m_targetVertex;
			}
		}

		outHull.Setup( std::move( data ) );

		// valid convex extracted
		return true;
	}

} // red