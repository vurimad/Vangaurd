/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiValueHolder.h"
#include "rttiPointerTypes.h"
#include "rttiPointerTypesImpl.h"
#include "rttiUtils.h"
#include "resource.h"
#include "serializationMapping.h"
#include "resourceReference.h"
#include "resourceAsyncReference.h"
#include "../../redFileSystem/include/file.h"
#include "serializable.h"
#include "handleSerialization.h"
#include "../../redCore/include/absolutePath.h"

namespace rtti
{

	static Bool CheckTypeCompatibility( const rtti::ClassType* pointedType, const String& pathString )
	{
		// non-resource classes are not compatible with any path
		if (!pointedType->IsA(ClassID< CResource >()))
			return false;

		const auto pathExt = red::paths::GetExtension(pathString);

		// a resource class can have many classes derived from it, any matching resource will fit here
		red::DynArray< const rtti::ClassType* > allSupportedClasses{ red::PoolEngine() };
		GetRttiSystem().EnumClasses(pointedType, allSupportedClasses);

		for ( const auto* resourceClass : allSupportedClasses )
		{
			const String resourceClassNewExt = resourceClass->GetDefaultObject< CResource >()->GetExtension();
			const String resourceClassDeprecatedExt = resourceClass->GetDefaultObject< CResource >()->GetDeprecatedExtension();
			if ( resourceClassNewExt == pathExt || resourceClassDeprecatedExt == pathExt )
			{
				return true;
			}
		}

		// no resource extensions matched
		return false;
	}

	//////////////////////////////////////////////////////////////////////////

	PointerType::PointerType( const rtti::IType *pointedType /*= NULL*/ )
		: m_name( FormatPointerTypeName( pointedType->GetName() ) )
		, m_refName( FormatScriptedReferenceTypeName( m_name ) )
	{
		RED_FATAL_ASSERT( pointedType->GetType() == RT_Class, "Only pointers to classes are suported by RTTI" );
		m_pointedClass = static_cast< const rtti::ClassType* >( pointedType );
	}

	void PointerType::Construct( void *object ) const
	{	
		*( Uint8** )object = NULL;
	}

	void PointerType::Destruct( void *object ) const
	{
		// no deletes or sth - memory allocation and object construction/destruction
		// should be managed by external code
		*( Uint8** )object = NULL;
	}

	Bool PointerType::Compare( const void* data1, const void* data2, Uint32 ) const
	{
		// Easy case
		void* ptr1 = *(void**) data1;
		void* ptr2 = *(void**) data2;
		if ( ptr1 == ptr2 )
		{
			return true;
		}

		// Not equal
		return false;
	}

	void PointerType::Copy( void* dest, const void* src ) const
	{
		*( Uint8** ) dest = *( Uint8* const * ) src;
	}

	Bool PointerType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		if ( file.IsWriter() )
		{
			serialization::IMapper::ObjectIndex objectIndex = 0;

			if ( file.HasMapper() )
			{
				SerializableHandle serializable( *reinterpret_cast< ISerializable** >( data ) );
				file.GetMapper()->MapPointer( serializable, objectIndex );
			}

			file << objectIndex;
		}
		else if ( file.IsReader() )
		{
			serialization::IMapper::ObjectIndex objectIndex = 0;
			file << objectIndex;

			SerializableHandle object;
			if ( file.HasMapper() )
				file.GetMapper()->UnmapPointer( objectIndex, object );

			*(void**)data = object.Get();
		}

		return true;
	}

	Bool PointerType::ToString( const void* object, String& valueString ) const
	{
		// Always show the NULL pointer
		void* ptr = * ( ISerializable** ) object;
		if ( NULL == ptr )
		{
			valueString = String::CreateExternal("NULL");
			return true;
		}

		// Other pointers cannot be translated
		return false;
	}

	Bool PointerType::FromString( void* object, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			*( void** ) object = NULL;
			return true;
		}

		return false;
	}

	const rtti::ClassType* PointerType::GetPointedType() const
	{
		return const_cast< const rtti::ClassType* >( m_pointedClass );
	}

	rtti::Pointer PointerType::GetPointer( const void* data ) const
	{
		RED_FATAL_ASSERT( m_pointedClass->IsA( ClassID< ISerializable >() ), "Only handles to ISerializables are allowed by RTTI" );

		// Get pointed data
		ISerializable* obj = NULL;
		Copy( &obj, data );

		// Create pointer from serializable ( it has virtual getClass() )
		return rtti::Pointer( obj );
	}

	void PointerType::SetPointer( void* data, const rtti::Pointer & ptr ) const
	{
		RED_FATAL_ASSERT( m_pointedClass->IsA( ClassID< ISerializable >() ), "Only handles to ISerializables are allowed by RTTI" );

		// Check type match
		if ( !ptr.IsNull() && !ptr.GetClass()->IsA( m_pointedClass ) )
		{
			RED_LOG_ERROR( "Core: Trying to assign object of type '%hs' to pointer '%hs'", 
				ptr.GetClass()->GetName().AsChar(), 
				m_pointedClass->GetName().AsChar() );
			return;
		}

		// Save data
		* ( void** ) data = (void*) ptr.GetPointer();
	}

	void PointerType::ClonePointer( void* data, const rtti::Pointer & ptr ) const
	{}

	const red::memory::Pool & PointerType::GetInnerTypeMemoryPool() const
	{
		return m_pointedClass->GetInnerTypeMemoryPool();
	}

	//////////////////////////////////////////////////////////////////////////

	HandleType::HandleType( const rtti::IType *pointedType /*= NULL*/ )
		: m_name( FormatHandleTypeName( pointedType->GetName() ) )
		, m_refName( FormatScriptedReferenceTypeName( m_name ) )
	{
		RED_FATAL_ASSERT( pointedType->GetType() == RT_Class, "Only handles to classes are suported by RTTI" );
		m_pointedClass = static_cast< const rtti::ClassType* >( pointedType );
	}

	void HandleType::Construct( void *object ) const
	{
		new ( object ) SerializableHandle();
	}

	void HandleType::Destruct( void *object ) const
	{
		SerializableHandle* handle = ( SerializableHandle* ) object;
		handle->Reset();
	}

	Bool HandleType::Compare( const void* data1, const void* data2, Uint32 flags ) const
	{
		// Easy compare
		const SerializableHandle& handle1 = * ( const SerializableHandle* )( data1 );
		const SerializableHandle& handle2 = * ( const SerializableHandle* )( data2 );
		if ( handle1.Get() == handle2.Get() )
		{
			return true;
		}
		else if( handle1 && handle2 && flags )
		{
			const ClassType* handle1ClassType = handle1->GetClass();
			const ClassType* handle2ClassType = handle2->GetClass();

			if( handle1ClassType == handle2ClassType )
			{
				return handle1ClassType->Compare( handle1.Get(), handle2.Get(), flags );			
			}
		}

		// Not equal
		return false;
	}

	void HandleType::Copy( void* dest, const void* src ) const
	{
		SerializableHandle& handleDest = * ( SerializableHandle* )( dest );
		const SerializableHandle& handleSrc = * ( const SerializableHandle* )( src );
		handleDest = handleSrc;
	}

	Bool HandleType::Serialize( IFile& file, void* data, ISerializable* owner  ) const
	{
		auto* handle = (SerializableHandle*) data;
		file << *handle ;
		return true;
	}

	Bool HandleType::ToString( const void* data, String& valueString ) const
	{
		const SerializableHandle& handle = *( const SerializableHandle* )( data );

		// Easy cases
		if ( !handle.Get() )
		{
			valueString = "NULL";
			return true;
		}

		// Get the object
		const ISerializable* ptr = static_cast< const ISerializable* >( handle.Get() );
		if ( ptr && ptr->GetClass()->IsA< CResource >() )
		{
			const CResource* res = static_cast< const CResource* >( ptr );
			if ( res->GetPath().IsValid() )
			{
				valueString = res->GetPath().ToString();
				return true;
			}
		}

		// Unable to convert to string
		return false;
	}

	Bool HandleType::FromString( void* data, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			*(SerializableHandle* )data = SerializableHandle();
			return true;
		}

		if ( m_pointedClass->IsA< CResource >() )
		{
			// Load resource, no stats
			auto token = GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( valueString ) );
			auto res = token->WaitUntilLoaded();
			if ( res && res->IsA( m_pointedClass ) )
			{
				SerializableHandle& handle = *(SerializableHandle*)( data );
				handle = token->GetResource();
				return true;
			}
		}

		// not set
		return false;
	}

	void* HandleType::GetPointed( void *pointerData ) const
	{
		SerializableHandle& handle = * ( SerializableHandle* )( pointerData );
		return handle.Get();
	}

	const rtti::ClassType* HandleType::GetPointedType() const
	{
		return m_pointedClass;
	}

	rtti::Pointer HandleType::GetPointer( const void* data ) const
	{
		const SerializableHandle& handle = * ( const SerializableHandle* )( data );
		return rtti::Pointer( handle.Get(), GetPointedType() );
	}

	void HandleType::SetPointer( void* data, const rtti::Pointer & ptr ) const
	{
		SerializableHandle& handle = * ( SerializableHandle* )( data );
		handle = HandleFromPtr( ptr.GetSerializablePtr() );
	}

	void HandleType::ClonePointer( void* data, const rtti::Pointer & ptr ) const
	{
		SerializableHandle& handle = * ( SerializableHandle* )( data );

		if( !ptr.IsNull() )
		{
			const rtti::ClassType * classType = ptr.GetClass();
			SerializableHandle clone = classType->CreateHandle< ISerializable >();
			classType->Copy( clone.Get(), ptr.GetSerializablePtr() );
		}
		else
		{
			handle.Reset();
		}

	}

	const CName HandleType::GetName() const 
	{ 
		return m_name; 
	} 

	Uint32 HandleType::GetSize() const 
	{ 
		return sizeof( SerializableHandle ); 
	}

	Uint32 HandleType::GetAlignment() const 
	{ 
		return __alignof( SerializableHandle ); 
	} 

	ERTTITypeType HandleType::GetType() const 
	{ 
		return RT_Handle; 
	}

	CName HandleType::GetRefName() const
	{
		return m_refName;
	}

	void HandleType::RebuildParentHierarchy( void * object, ISerializable * parent ) const
	{
		SerializableHandle& handle = * ( SerializableHandle* )( object );
		if( handle )
		{
			const rtti::ClassType * instanceClassType = handle->GetClass();
			instanceClassType->RebuildParentHierarchy( handle.Get(), parent );
		}
	}
	
	const red::memory::Pool & HandleType::GetInnerTypeMemoryPool() const
	{
		return m_pointedClass->GetInnerTypeMemoryPool();
	}

	//////////////////////////////////////////////////////////////////////////

	WeakHandleType::WeakHandleType( const rtti::IType *pointedType /*= NULL*/ )
		: m_name( FormatWeakHandleTypeName( pointedType->GetName() ) )
		, m_refName( FormatScriptedReferenceTypeName( m_name ) )
	{
		RED_FATAL_ASSERT( pointedType->GetType() == RT_Class, "Only handles to classes are suported by RTTI" );
		m_pointedClass = static_cast< const rtti::ClassType* >( pointedType );
	}

	void WeakHandleType::Construct( void *object ) const
	{
		new ( object ) SerializableWeakHandle();
	}

	void WeakHandleType::Destruct( void *object ) const
	{
		SerializableWeakHandle* handle = ( SerializableWeakHandle* ) object;
		handle->Reset();
	}

	Bool WeakHandleType::Compare( const void* data1, const void* data2, Uint32 ) const
	{
		// Easy compare
		const SerializableHandle handle1 = (( const SerializableWeakHandle* )( data1 ))->ToHandle();
		const SerializableHandle handle2 = (( const SerializableWeakHandle* )( data2 ))->ToHandle();
		if ( handle1.Get() == handle2.Get() )
		{
			return true;
		}

		// Not equal
		return false;
	}

	void WeakHandleType::Copy( void* dest, const void* src ) const
	{
		SerializableWeakHandle& handleDest = * ( SerializableWeakHandle* )( dest );
		const SerializableWeakHandle& handleSrc = * ( const SerializableWeakHandle* )( src );
		handleDest = handleSrc;
	}

	Bool WeakHandleType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
 		auto* handle = (SerializableWeakHandle*) data;
 		file << *handle ;
		return true;
	}

	Bool WeakHandleType::ToString( const void* data, String& valueString ) const
	{
		const SerializableHandle handle = (( const SerializableWeakHandle* )( data ))->ToHandle();

		// Easy cases
		if ( !handle.Get() )
		{
			valueString = "NULL";
			return true;
		}

		// Get the object
		const ISerializable* ptr = static_cast< const ISerializable* >( handle.Get() );
		if ( ptr && ptr->GetClass()->IsA< CResource >() )
		{
			const CResource* res = static_cast< const CResource* >( ptr );
			if ( res->GetPath().IsValid()  )
			{
				valueString = res->GetPath().ToString();
				return true;
			}
		}

		// Unable to convert to string
		return false;
	}

	Bool WeakHandleType::FromString( void* data, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			*(SerializableWeakHandle* )data = SerializableWeakHandle();
			return true;
		}

		// not set
		return false;
	}

	void* WeakHandleType::GetPointed( void *pointerData ) const
	{
		SerializableHandle handle = (( SerializableWeakHandle* )( pointerData ))->ToHandle();
		return handle.Get();
	}

	const rtti::ClassType* WeakHandleType::GetPointedType() const
	{
		return const_cast< const rtti::ClassType* >( m_pointedClass );
	}

	rtti::Pointer WeakHandleType::GetPointer( const void* data ) const
	{
		const SerializableHandle& handle = (( const SerializableWeakHandle* )( data ))->ToHandle();
		return rtti::Pointer( handle.Get(), GetPointedType() );
	}

	void WeakHandleType::SetPointer( void* data, const rtti::Pointer & ptr ) const
	{
		SerializableWeakHandle& handle = * ( SerializableWeakHandle* )( data );
		if( ISerializable* obj = ptr.GetSerializablePtr() )
		{
			handle = obj->HandleFromThis();
		}
		else
		{
			handle = nullptr;
		}
	}

	void WeakHandleType::ClonePointer( void* data, const rtti::Pointer & ptr ) const
	{}

	const CName WeakHandleType::GetName() const 
	{ 
		return m_name; 
	} 

	Uint32 WeakHandleType::GetSize() const 
	{ 
		return sizeof( SerializableWeakHandle ); 
	}

	Uint32 WeakHandleType::GetAlignment() const 
	{ 
		return __alignof( SerializableWeakHandle ); 
	} 

	ERTTITypeType WeakHandleType::GetType() const 
	{ 
		return RT_WeakHandle; 
	}

	CName WeakHandleType::GetRefName() const
	{
		return m_refName;
	}

	const red::memory::Pool & WeakHandleType::GetInnerTypeMemoryPool() const
	{
		return m_pointedClass->GetInnerTypeMemoryPool();
	}

	//////////////////////////////////////////////////////////////////////////
	// ResourceReferenceType
	//////////////////////////////////////////////////////////////////////////
	ResourceReferenceType::ResourceReferenceType( const rtti::IType* pointedType /*nullptr*/ )
		: m_name( rtti::FormatResRefTypeName( pointedType->GetName() ) )
		, m_refName( rtti::FormatScriptedReferenceTypeName( m_name ) )
	{
		m_pointedClass = static_cast< const rtti::ClassType* >( pointedType );
	}
	
	const rtti::ClassType* ResourceReferenceType::GetPointedType() const
	{
		return const_cast< const rtti::ClassType* >( m_pointedClass );
	}

	const CName ResourceReferenceType::GetName() const
	{
		return m_name;
	}

	Uint32 ResourceReferenceType::GetSize() const
	{
		return sizeof( res::ResourceReference );
	}

	Uint32 ResourceReferenceType::GetAlignment() const 
	{ 
		return __alignof( res::ResourceReference );
	} 

	ERTTITypeType ResourceReferenceType::GetType() const 
	{ 
		return RT_ResourceReference;
	}

	CName ResourceReferenceType::GetRefName() const
	{
		return m_refName;
	}

	void ResourceReferenceType::Construct( void* object ) const
	{
		new ( object ) res::ResourceReference();
	}

	void ResourceReferenceType::Destruct( void* object ) const
	{
		res::ResourceReference* handle = ( res::ResourceReference* ) object;
		handle->Clear();
	}

	Bool ResourceReferenceType::Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const
	{
		const res::ResourceReference& ref1 = * ( const res::ResourceReference* )( data1 );
		const res::ResourceReference& ref2 = * ( const res::ResourceReference* )( data2 );

		return ref1.GetPath() == ref2.GetPath();
	}

	void ResourceReferenceType::Copy( void* dest, const void* src ) const
	{
		res::ResourceReference& refDest = * ( res::ResourceReference* )( dest );
		const res::ResourceReference& refSrc = * ( const res::ResourceReference* )( src );
		refDest = refSrc;
	}

	Bool ResourceReferenceType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		res::ResourceReference& handle = * ( res::ResourceReference* )( data );
		handle.Serialize( file );
		return true;
	}

	Bool ResourceReferenceType::ToString(const void* object, String& valueString) const
	{
		const res::ResourceReference* resRef = reinterpret_cast< const res::ResourceReference* >( object );
		if ( resRef->GetPath().IsValid() )
		{
			valueString = resRef->GetPath().ToString();
		}
		else
		{
			valueString = "NULL";
		}

		return true;
	}

	Bool ResourceReferenceType::FromString( void* object, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			res::ResourceReference& resRef = * ( res::ResourceReference* )( object );
			resRef.Clear();
			return true;
		}

		RED_FATAL_ASSERT( m_pointedClass->IsA< CResource >(), "Resource reference with something else than a resource - WTF?" );

		// Initialize from path only, no loading
		res::ResourceReference rewResRef( res::ResourcePath::Build( valueString ) );
		*static_cast< res::ResourceReference* >( object ) = rewResRef;

		return true;
	}

	const red::memory::Pool & ResourceReferenceType::GetInnerTypeMemoryPool() const
	{
		return m_pointedClass->GetInnerTypeMemoryPool();
	}

	res::ResourceReference * ResourceReferenceType::GetResourceReference( void * data ) const
	{
		return static_cast< res::ResourceReference * >( data );
	}

	//////////////////////////////////////////////////////////////////////////
	// ResourceAsyncReferenceType
	//////////////////////////////////////////////////////////////////////////
	ResourceAsyncReferenceType::ResourceAsyncReferenceType( const rtti::IType* pointedType /*= nullptr */ )
		: m_name( rtti::FormatResAsyncRefTypeName( pointedType->GetName() ) )
		, m_refName( rtti::FormatScriptedReferenceTypeName( m_name ) )
	{
		m_pointedClass = static_cast< const rtti::ClassType* >( pointedType );
	}

	const rtti::ClassType* ResourceAsyncReferenceType::GetPointedType() const 
	{
		return const_cast< const rtti::ClassType* >( m_pointedClass );
	}

	const CName ResourceAsyncReferenceType::GetName() const
	{
		return m_name;
	}

	Uint32 ResourceAsyncReferenceType::GetSize() const
	{
		return sizeof( res::ResourceAsyncReference );
	}

	Uint32 ResourceAsyncReferenceType::GetAlignment() const
	{
		return __alignof( res::ResourceAsyncReference );
	}

	ERTTITypeType ResourceAsyncReferenceType::GetType() const
	{
		return RT_ResourceAsyncReference;
	}

	CName ResourceAsyncReferenceType::GetRefName() const
	{
		return m_refName;
	}

	void ResourceAsyncReferenceType::Construct( void *object ) const
	{
		new ( object ) res::ResourceAsyncReference();
	}

	void ResourceAsyncReferenceType::Destruct( void *object ) const
	{
		res::ResourceAsyncReference* handle = ( res::ResourceAsyncReference* ) object;
		handle->~ResourceAsyncReference();
	}

	Bool ResourceAsyncReferenceType::Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const
	{
		const res::ResourceAsyncReference& ref1 = * ( const res::ResourceAsyncReference* )( data1 );
		const res::ResourceAsyncReference& ref2 = * ( const res::ResourceAsyncReference* )( data2 );

		return ref1.GetPath() == ref2.GetPath();
	}

	void ResourceAsyncReferenceType::Copy( void* dest, const void* src ) const
	{
		res::ResourceAsyncReference& refDest = * ( res::ResourceAsyncReference* )( dest );
		const res::ResourceAsyncReference& refSrc = * ( const res::ResourceAsyncReference* )( src );
		refDest = refSrc;
	}

	Bool ResourceAsyncReferenceType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		res::ResourceAsyncReference& handle = * ( res::ResourceAsyncReference* )( data );
		handle.Serialize( file );
		return true;
	}
	const Bool ResourceAsyncReferenceType::WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const
	{
		if( rtti::IType::WriteValue( ctx, data, path, newValue, clone ) )
		{
			if( newValue.IsEmpty() )
			{
				// if value holder is empty, rtti::IType Destructs() the object under data pointer,
				// but some classes that use TSimpleTypeRawSerialization don't have d-tors
				// - hence we Construct() default object
				Construct( data );
			}
			return true;
		}
		return false;
	}


	Bool ResourceAsyncReferenceType::ToString(const void* object, String& valueString) const
	{
		const res::ResourceAsyncReference* resRef = reinterpret_cast< const res::ResourceAsyncReference* >( object );
		if ( resRef->GetPath().IsValid() )
		{
			valueString = resRef->GetPath().ToString();
		}
		else
		{
			valueString = "NULL";
		}

		return true;
	}

	Bool ResourceAsyncReferenceType::FromString( void* object, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			res::ResourceAsyncReference& resRef = * ( res::ResourceAsyncReference* )( object );
			resRef = res::ResourceAsyncReference();
			return true;
		}

		RED_FATAL_ASSERT( m_pointedClass->IsA< CResource >(), "Resource reference with something else than a resource - WTF?" );

		// Check type compatibility
		if ( !CheckTypeCompatibility( m_pointedClass, valueString ) )
			return false;

		// Initialize from path only, no loading
		res::ResourceAsyncReference rewResRef( res::ResourcePath::Build( valueString ) );
		*static_cast< res::ResourceAsyncReference* >( object ) = rewResRef;

		return true;
	}

	const red::memory::Pool & ResourceAsyncReferenceType::GetInnerTypeMemoryPool() const
	{
		return m_pointedClass->GetInnerTypeMemoryPool();
	}

	res::ResourceAsyncReference * ResourceAsyncReferenceType::GetResourceReference( void * data ) const
	{
		return static_cast< res::ResourceAsyncReference * >( data );
	}

	//////////////////////////////////////////////////////////////////////////

	ScriptedReferenceType::ScriptedReferenceType( const rtti::IType *pointedType /*= NULL*/ )
		: m_pointedType( pointedType )
		, m_pointedObject( nullptr )
	{
		if ( pointedType )
		{
			m_name = FormatScriptedReferenceTypeName( pointedType->GetName() );
		}
	}

	CName ScriptedReferenceType::GetRefName() const
	{
		return FormatScriptedReferenceTypeName( m_name );
	}

	void ScriptedReferenceType::Construct( void *object ) const
	{
		// nothing to do here
	}

	void ScriptedReferenceType::Destruct( void *object ) const
	{
		ScriptedReferenceType* typedObj = static_cast< ScriptedReferenceType* >( object );
		typedObj->m_pointedType = nullptr;
		typedObj->m_pointedObject = nullptr;
		typedObj->m_name.Set( CName::NONE() );
	}

	Bool ScriptedReferenceType::Compare( const void* data1, const void* data2, Uint32 ) const
	{
		const ScriptedReferenceType* typedData1 = static_cast< const ScriptedReferenceType* >( data1 );
		const ScriptedReferenceType* typedData2 = static_cast< const ScriptedReferenceType* >( data2 );
		return typedData1->m_pointedType == typedData2->m_pointedType && typedData1->m_pointedObject == typedData2->m_pointedObject;
	}

	void ScriptedReferenceType::Copy( void* dest, const void* src ) const
	{
		ScriptedReferenceType* typedDest = static_cast< ScriptedReferenceType* >( dest );
		const ScriptedReferenceType* typedSrc = static_cast< const ScriptedReferenceType* >( src );
		typedDest->m_pointedType = typedSrc->m_pointedType;
		typedDest->m_pointedObject = typedSrc->m_pointedObject;
		typedDest->m_name = typedSrc->m_name;
	}

	Bool ScriptedReferenceType::Serialize( IFile& file, void* data, ISerializable* owner ) const
	{
		if ( file.IsWriter() )
		{
			serialization::IMapper::ObjectIndex objectIndex = 0;

			if ( file.HasMapper() )
			{
				SerializableHandle serializable( *reinterpret_cast< ISerializable** >( data ) );
				file.GetMapper()->MapPointer( serializable, objectIndex );
			}

			file << objectIndex;
		}
		else if ( file.IsReader() )
		{
			serialization::IMapper::ObjectIndex objectIndex = 0;
			file << objectIndex;

			SerializableHandle object;
			if ( file.HasMapper() )
				file.GetMapper()->UnmapPointer( objectIndex, object );

			*(void**)data = object.Get();
		}

		return true;
	}

	Bool ScriptedReferenceType::ToString( const void* object, String& valueString ) const
	{
		const ScriptedReferenceType* ptr = static_cast< const ScriptedReferenceType* >( object );
		if ( !ptr || !( ptr->m_pointedObject ) || !( ptr->m_pointedType ) )
		{
			valueString = String::CreateExternal( "NULL" );
		}
		else 
		{
			return ptr->m_pointedType->ToString( ptr->m_pointedObject, valueString );
		}

		return true;
	}

	Bool ScriptedReferenceType::FromString( void* object, const String& valueString ) const
	{
		if ( valueString.EqualsNC( "NULL" ) )
		{
			*( void** ) object = NULL;
			return true;
		}

		return false;
	}

	const red::memory::Pool & ScriptedReferenceType::GetInnerTypeMemoryPool() const
	{
		return m_pointedType->GetInnerTypeMemoryPool();
	}

	void ScriptedReferenceType::SetReference( const rtti::IType *pointedType, void* data )
	{
		m_pointedObject = data;
		m_pointedType = pointedType;

		if ( pointedType )
		{
			const CName existingNameRef = pointedType->GetRefName();
			if( existingNameRef != CName::NONE() )
			{
				m_name = existingNameRef;
			}
			else
			{
				m_name = FormatScriptedReferenceTypeName( pointedType->GetName() );
			}
		}
	}

} // rtti