
#include "build.h"
#include "backendInteropUtils.h"

#include "../../common/interop/include/interopInterchange.h"
#include "../../common/redReflection/include/resource.h"

namespace helper
{
	RTTIClassDumpMapper::RTTIClassDumpMapper( interop::RTTIClassDump& outDump, Uint32 numClasses )
		: m_outDump( outDump )
		, m_nextIndex( 0 )
	{
		m_indexMap.Reserve( numClasses );
		m_outDump.m_entries.Resize( numClasses );
		m_outDump.m_classNames.Resize( numClasses );
		m_outDump.m_descriptiveNames.Resize( numClasses );
	}

	void RTTIClassDumpMapper::MapClass( const rtti::ClassType& classType )
	{
		const Int32 classIndex = MapSingleClass( classType );
		const Int32 baseClassIndex = classType.GetBaseClass() ? MapSingleClass( *classType.GetBaseClass() ) : -1;
		const Int32 resourceIndex = TryMapResource( classType );
		const Bool isAbstract = classType.IsAbstract();
		m_outDump.m_entries[ classIndex ] = interop::RTTIClassDumpEntry( classIndex, baseClassIndex, resourceIndex, isAbstract );
	}

	Bool RTTIClassDumpMapper::VerifyAllClassesMapped()
	{
		return m_indexMap.Size() == m_outDump.m_entries.Size();
	}

	Int32 RTTIClassDumpMapper::MapSingleClass( const rtti::ClassType& classType )
	{
		const CName className = classType.GetName();
		RED_FATAL_ASSERT( className );
		auto res = m_indexMap.Insert( className, m_nextIndex );
		const Int32 classIndex = res.Iterator().Value();
		if ( res.IsSuccessful() )
		{
			m_nextIndex += 1;
			m_outDump.m_classNames[ classIndex ] = classType.GetName().AsChar();
			m_outDump.m_descriptiveNames[ classIndex ] = classType.GetDescriptiveName();
		}

		return classIndex;
	}

	Int32 RTTIClassDumpMapper::TryMapResource( const rtti::ClassType& classType )
	{
		Int32 resourceIndex = -1;
		if ( !classType.IsAbstract() && classType.IsA( CResource::GetStaticClass() ) )
		{
			CResource* resource = classType.GetDefaultObject<CResource>();
			if ( resource )
			{
				resourceIndex = m_outDump.m_resourceInfos.Size();
				m_outDump.m_resourceInfos.PushBack( interop::RTTIResourceDumpInfo( resource->GetExtension(), resource->GetDeprecatedExtension(), resource->GetFriendlyDescription() ) );
			}
		}
		return resourceIndex;
	}
}