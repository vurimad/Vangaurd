#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include <utility>
#include <iterator>

//////////////////////////////////////////////////////////////////////////
// macros
#define String_CreateExternal_OnStack( _MaxCapacity ) red::String::CreateExternal( static_cast<char*>( RED_ALLOCA( _MaxCapacity ) ), _MaxCapacity, true )
#define String_CreateExternal_OnStack_Set( _Buffer, _MaxCapacity ) std::move( red::String::CreateExternal( static_cast<char*>( RED_ALLOCA( _MaxCapacity ) ), _MaxCapacity, true ).Set( _Buffer ) )
#define String_CreateExternal_OnStack_SetN( _Buffer, _BufferLen, _MaxCapacity ) std::move( red::String::CreateExternal( static_cast<char*>( RED_ALLOCA( _MaxCapacity ) ), _MaxCapacity, true ).Set( _Buffer, _BufferLen ) )

#define RED_CONTAINER_STRING_DEFAULT_POOL = red::PoolString()
#define RED_CONTAINER_STRING_DEFAULT_POOL_NAME red::PoolString

#if !defined( RED_CONFIGURATION_FINAL ) || defined( RED_USE_PROFILER )
#define RED_USE_STRING_STATS
#endif

//////////////////////////////////////////////////////////////////////////
// declarations
namespace red 
{
	template < typename TElement >
	class DynArray;

	template < typename TElement >
	class ArraySpan;

	class StringView;
	
	class RED_CONTAINERS_API String 
	{

	public:
		
		friend class StringDebugger;

		// typedefs
		typedef char									value_type;
		typedef char*									iterator;
		typedef const char*								const_iterator;
		typedef std::reverse_iterator<iterator>			reverse_iterator;
		typedef std::reverse_iterator<const_iterator>	const_reverse_iterator;

		enum : Uint32 { npos = std::numeric_limits<Uint32>::max() };

		// enums
		enum EStringMode
		{
			SM_Internal			= 0,
			SM_Dynamic			= 1,
			SM_External			= 2,
			SM_MAX,
		};

		// reversed wrapper to string (range-based for-loop)
		struct ReversedWrapper
		{
		private:
			String& str;
		public:
			ReversedWrapper( String& _str ) : str( _str ) {}
			const_reverse_iterator cbegin()	{ return str.crbegin(); }
			const_reverse_iterator cend()	{ return str.crend(); }
			reverse_iterator begin()		{ return str.rbegin(); }
			reverse_iterator end()			{ return str.rend(); }
		};

		// empty string access
		static const String& EMPTY();

		// statics
		static const Uint32 ALIGNMENT = 8;

		// ctor/dtor
		String();
		String(const char* str);
		String(const char* str, const Uint32 length);
		String(const String& str);
		String(String&& str);
		explicit String(const Uint32 initCapacity);
		explicit String(char _char);
		~String();

		// ctor / dtor pool
		explicit String(const red::memory::Pool &pool);
		explicit String(const char* str, const red::memory::Pool &pool);
		explicit String(const char* str, const Uint32 length, const red::memory::Pool &pool);
		explicit String(const String& str, const red::memory::Pool &pool);
		explicit String(const Uint32 initCapacity, const red::memory::Pool &pool);
		explicit String(char _char, const red::memory::Pool &pool);

		// operators
		String& operator=( const char* cstr );
		String& operator=( const String& str );
		String& operator=( String&& str );
		String& operator+=( char character );
		String& operator+=( const char* cstr );
		String& operator+=( const String& str );
		String& operator+=( const StringView& str );

		Bool operator==( const String& str ) const;
		Bool operator!=( const String& str ) const;
		Bool operator==( const char* str ) const;
		Bool operator!=( const char* str ) const;
		const char& operator[]( Uint32 i ) const;
		char& operator[]( Uint32 i );

		char Front() const;
		char Back() const;

		// comparators
		const Bool EqualsNC( const char* cstr ) const;
		const Bool EqualsNC( const String& str ) const;
		static Bool Less( const char* buf1, size_t num1, const char* buf2, size_t num2 );

		// buffer accessors
		const char* AsChar() const;
		char* AsChar();
		const void* Data() const;
		void* Data();

		// misc
		static String CreateExternal( const char* cstr, const Uint32 maxCapacity = 0, const Bool nullTerminate = false );

		// sizes
		Bool Empty() const;
		Uint32 DataSize() const;
		Uint32 Capacity() const;
		Uint32 Length() const;
		Uint32 Size() const { return Length(); }
		constexpr Uint32 MaxSize() const { return DYNAMIC_MAX_LENGTH_MASK ^ std::numeric_limits< Uint32 >::max(); } // 2^30-1

		// setters
		String& Set( const char* cstr );
		String& Set( const char* cstr, const Uint32 length );
		String& Set( const String& str );
		String& Set( const StringView& str );

		void Swap( String& str );

		// inserters
		Bool Insert( const Uint32 index, const char newChar );
		Bool Insert( const Uint32 index, const char* subCString );
		bool Insert( const Uint32 index, const char* subString, const Uint32 length );
		Bool Insert( const Uint32 index, const String& subString );
		bool Insert( const Uint32 index, const StringView& subString );
		String& Append( const char* buf, const Uint32 size );
		String& Append( const char newChar );
		String& Append( const String& str );
		String& Append( const StringView& str );

		// clear/remove/erase
		void Clear();
		const Bool Erase( iterator where );
		const Bool Erase( iterator first, iterator last );
		Bool Erase( Uint32 startIndex, Uint32 endIndex );
		Bool Remove( const char& element );
		Bool RemoveAt( const Uint32 index );

		// resizers
		Bool Reserve(const Uint32 newLen);
		Bool Resize(const Uint32 newLen);

		// substrings - to refactor on last pass
		String LeftString( Uint32 count ) const;
		String RightString( Uint32 count ) const;
		String MidString( Uint32 start, Uint32 count = std::numeric_limits<Int32>::max() ) const;
		String StringBefore( const String& separator ) const;
		String StringAfter( const String& separator ) const;
		String StringBeforeFromRight( const String& separator ) const;
		String StringAfterFromRight( const String& separator ) const;

		// red iterators
		RED_INLINE const_iterator Begin() const	{ return AsChar(); }
		RED_INLINE const_iterator End() const { return AsChar() + Length(); }
		RED_INLINE iterator Begin() { return AsChar(); }
		RED_INLINE iterator End() { return AsChar() + Length(); }

		// contains
		const Bool Contains( const char wanterChar, const Uint32 startIndex = 0 ) const;
		const Bool Contains( const char* wantedCString, const Uint32 startIndex = 0 ) const;
		const Bool Contains( const String& wantedString, const Uint32 startIndex = 0 ) const;
		const Bool ContainsNoCase( const char wantedChar, const Uint32 startIndex = 0 ) const;
		const Bool ContainsNoCase( const char* wantedCString, const Uint32 startIndex = 0 ) const;
		const Bool ContainsNoCase( const String& wantedString, const Uint32 startIndex = 0 ) const;
		const Bool ContainsFromRight( const char wantedChar, const Uint32 startIndex = String::npos ) const;
		const Bool ContainsFromRight( const char* wantedCString, const Uint32 startIndex = String::npos ) const;
		const Bool ContainsFromRight( const String& wantedString, const Uint32 startIndex = String::npos ) const;
		const Bool ContainsFromRightNoCase( const char wantedChar, const Uint32 startIndex = String::npos ) const;
		const Bool ContainsFromRightNoCase( const char* wantedCString, const Uint32 startIndex = String::npos ) const;
		const Bool ContainsFromRightNoCase( const String& wantedString, const Uint32 startIndex = String::npos ) const;

		// index of
		const Bool IndexOf( const char wanterChar, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOf( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOf( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOfNoCase( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOfNoCase( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOfNoCase( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex = 0 ) const;
		const Bool IndexOfLast( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;
		const Bool IndexOfLast( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;
		const Bool IndexOfLast( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;
		const Bool IndexOfLastNoCase( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;
		const Bool IndexOfLastNoCase( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;
		const Bool IndexOfLastNoCase( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex = String::npos ) const;

		// todo: to refactor on last pass
		Uint32 CountChars( char c ) const;
		Bool MatchAny( const red::DynArray< String >& filters ) const;
		Bool MatchAll( const red::DynArray< String >& filters ) const;
		Bool EndsWith( const String& str ) const;
		Bool BeginsWith( const String& str ) const;
		Bool BeginsWithNoCase( const String& str ) const;

		// trimmers
		String& TrimLeft();
		String& TrimLeft( char c );
		String& TrimRight();
		String& TrimRight( char c );
		String& Trim();
		String TrimCopy() const;
		void RemoveWhiteSpaces();
		void RemoveWhiteSpacesAndQuotes();

		// replacers
		void ReplaceAll( char src, char target);
		Bool ReplaceAll( const String& src, const String& target, const red::memory::Pool &pool RED_CONTAINER_STRING_DEFAULT_POOL);
		Bool Replace( const String& src, const String& target, Bool rightSide = false );
		void Replace( char src, char target, Uint32 startIndex = 0, Uint32 endIndex = String::npos );

		// splitters
		red::DynArray< String > Split( const String& separator, bool includeEmpty = false, const red::memory::Pool &pool RED_CONTAINER_STRING_DEFAULT_POOL ) const;
		red::DynArray< String > Split( const red::DynArray< String >& separators, const red::memory::Pool &pool RED_CONTAINER_STRING_DEFAULT_POOL ) const;

		const Bool Split(const String& separator, String* leftPart, String* rightPart) const;
		const Bool Split(const red::DynArray< String >& separators, String* leftPart, String* rightPart) const;

		const Bool SplitFromRight( const String& separator, String* leftPart, String* rightPart ) const;
		const Bool SplitFromRight( const red::DynArray< String >& separators, String* leftPart, String* rightPart ) const;

		// ** tokenizers to refactor
		void Slice( red::DynArray< String >& parts, const String& separator ) const;
		Uint32 GetTokens( const char delim, Bool unique, red::DynArray< String >& tokens ) const;

		// Join parts using given glue - to refactor
		static String Join( const red::ArraySpan< const String >& parts, const String& glue, const red::memory::Pool &pool RED_CONTAINER_STRING_DEFAULT_POOL);

		// casing
		String& ToLower();
		String& ToUpper();

		// formatters
		static String PrintfV(STATIC_CHECK_PRINTF_MSC const char* format, va_list arglist);
		static String PrintfV(const red::memory::Pool &pool, STATIC_CHECK_PRINTF_MSC const char* format, va_list arglist);

		static String Printf(STATIC_CHECK_PRINTF_MSC const char* format, ...);
		static String Printf(const red::memory::Pool &pool, STATIC_CHECK_PRINTF_MSC const char* format, ...);

		// todo: elders - to refactor on last pass
		RED_INLINE Uint32 CalcHash() const { return SimpleHash( AsChar(), Length() ); }
		void SimpleHash( Uint32& hash ) const;
		static Uint32 SimpleHash( const char* text, const Uint32 length );
		static Uint32 SimpleHash( const Char* text, const Uint32 length );

		//////////////////////////////////////////////////////////////////////////
		// return reversed wrapper to string (range-based for-loop)
		RED_INLINE ReversedWrapper Reversed() { return *this; }

		//////////////////////////////////////////////////////////////////////////
		// iterators, range-based for-loop
		RED_INLINE iterator begin()						{ return AsChar(); }
		RED_INLINE iterator end()							{ return AsChar() + Length(); }
		RED_INLINE reverse_iterator rbegin()				{ return reverse_iterator( end() ); }
		RED_INLINE reverse_iterator rend()				{ return reverse_iterator( begin() ); }

		RED_INLINE const_iterator cbegin() const			{ return AsChar(); }
		RED_INLINE const_iterator cend() const			{ return AsChar() + Length(); }
		RED_INLINE const_reverse_iterator crbegin() const	{ return const_reverse_iterator( cend() ); }
		RED_INLINE const_reverse_iterator crend() const	{ return const_reverse_iterator( cbegin() ); }

		static const Uint32 STRING_CLASS_SIZE = 32;

	private:
		static const Uint32	INTERNAL_CAPACITY = STRING_CLASS_SIZE - sizeof(Uint64) - sizeof(Uint32);
		static const Uint32	EXTERNAL_FILLER_SIZE = (INTERNAL_CAPACITY - sizeof(char *) - sizeof(Uint32));

		struct Fast    // not anymore
		{
			Uint64	data0;
			Uint64	data1;
			Uint64	filler;
			Uint64	strInfo;
		};

		struct External
		{
			char* buf;
			char filler[EXTERNAL_FILLER_SIZE];
			Uint32 capacity;		// string capacity in bytes
			Uint32 length : 30;		// 2^30-1
			Uint32 mode : 2;		// EStringMode
			Uint64 poolAddr;
		};
		
		struct Internal
		{
			char buf[INTERNAL_CAPACITY];
			Uint32 length : 30;		// 2^30-1
			Uint32 mode : 2;		// EStringMode
			Uint64 poolAddr;		// pool 
		};

		// props
		union
		{
			External	e;
			Internal	i;
			Fast		f;
		};

		// private stuff
		void _Init();
		void _Init(const EStringMode mode, const red::memory::Pool &pool);

		void _ResizeBuffer( Uint32 newElementCount );
		void _ResizeDynamicBuffer(const Uint32 newElementCount);
		void _ResizeInternalBuffer(const Uint32 newElementCount);

		void _SetLength( const Uint32 newLen );
		void _Terminate( const Uint32 at );

		const Bool _IndexOf( const char wanterChar, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const;
		const Bool _IndexOf( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const;
		const Bool _IndexOfLast( const char wanterChar, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const;
		const Bool _IndexOfLast( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const;
		const Bool _Split( const String& separator, String* leftPart, String* rightPart, Bool rightSide ) const;
		const Bool _Split( const red::DynArray< String >& separators, String* leftPart, String* rightPart, Bool rightSide ) const;
		String _StringBefore( const String& separator, Bool rightSide ) const;
		String _StringAfter( const String& separator, Bool rightSide ) const;
		
		//////////////////////////////////////////////////////////////////////////
		// is string data stored in allocated dynamic buffer
		const Bool _IsDynamic() const;

		//////////////////////////////////////////////////////////////////////////
		// is string data stored internally in object without any external buffer
		const Bool _IsInternal() const;

		//////////////////////////////////////////////////////////////////////////
		// is string buffer provided by user (const max capacity)
		const Bool _IsExternal() const;

		const Bool _IsValid() const;

		//////////////////////////////////////////////////////////////////////////
		// check validity of string access
		RED_FORCE_INLINE void _BoundsCheck( const Uint32 index ) const
		{
			RED_FATAL_ASSERT( ( index >= 0 ) && ( index <= Length() ), "red::String: Out of bounds. Cannot access character %u as the string is only size %u [%p]", index, Length() + 1, AsChar() );
			RED_UNUSED( index );
		}

		static const Uint32	DYNAMIC_MAX_LENGTH_MASK = 0xc0000000;	// max dynamic string capacity mask
	};

	static_assert(sizeof(red::String) == red::String::STRING_CLASS_SIZE, "Invalid red::String size");

	//////////////////////////////////////////////////////////////////////////
	// helper class
	class RED_CONTAINERS_API StringDebugger
	{
	public:
		static const Bool IsDynamic( const String& str );
		static const Bool IsInternal( const String& str );
		static const Bool IsExternal( const String& str );
		static const Bool IsValid(const String& str);
		static const Bool IsAscii( const String& str );
	};

	 RED_CONTAINERS_API String operator+( const String& str1, const String& str2 );
	 RED_CONTAINERS_API Bool operator<( const String& lhs, const String& rhs );

	 //////////////////////////////////////////////////////////////////////////
	// Enable c++11 range-based for loop

	RED_CONTAINERS_API String::iterator begin( String& string );
	RED_CONTAINERS_API String::iterator end( String& string );
	RED_CONTAINERS_API String::const_iterator begin( const String& string );
	RED_CONTAINERS_API String::const_iterator end( const String& string );
	RED_CONTAINERS_API String::const_iterator cbegin( const String& string );
	RED_CONTAINERS_API String::const_iterator cend( const String& string );

	namespace err
	{
		// Used in a function expression and never stored; m_buf not owned and must remain valid
		struct StringViewConvertType
		{
			StringViewConvertType(const char* buf)
				: m_buf( buf )
			{}

			StringViewConvertType(const String& str)
				: m_buf( str.AsChar() )
			{}

			operator const char*() const { return m_buf;  }

			const char* m_buf;
		};
	}

	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<String, Length>
	{
		static_assert(Length <= 1024, "Length too long");
		using StorageType = char[DefaultCrashDataCharArrayLength<Length>() + 1];
		using SetType = StringViewConvertType;

		static CrashDataCopyResult Copy(StorageType& storage, const SetType& value)
		{
			const Uint32 destSize = RED_ARRAY_COUNT_U32(storage);
			if (!red::Strcpy(storage, value, destSize, destSize - 1))
			{
				return CrashDataCopyResult::Error;
			}

			const Uint32 srcLen = (Uint32)red::Strlen(value);
			return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			return red::Strcpy(buffer, val, bufferLen);
		}
	};

#if defined( RED_USE_STRING_STATS )
	RED_CONTAINERS_API Uint32 Debug_GetActiveStringCount();
	RED_CONTAINERS_API Uint32 Debug_GetActiveDynamicStringCount();
	RED_CONTAINERS_API Int64 Debug_GetDynamicStringMemory();
#endif

} // red

namespace StringHelpers
{
	RED_CONTAINERS_API Int32 StrchrR( const red::String& str, char ch );
	RED_CONTAINERS_API Bool WildcardMatch( const char* str, const char* match );
}

#if !defined(RED_MODULE_redContainers) && defined(RED_DLL)
	class RED_CONTAINERS_API red::String;
#endif


//////////////////////////////////////////////////////////////////////////
// EOF

