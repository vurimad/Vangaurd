#pragma once

namespace interop
{
	struct RTTIClassDump;
}

namespace helper
{
	class BACKEND_ENGINE_API RTTIClassDumpMapper
	{
	public:
		explicit RTTIClassDumpMapper( interop::RTTIClassDump& outDump, Uint32 numClasses );

		void MapClass( const rtti::ClassType& classType );
		Bool VerifyAllClassesMapped();

	private:
		Int32 MapSingleClass( const rtti::ClassType& classType );
		Int32 TryMapResource( const rtti::ClassType& classType );

		interop::RTTIClassDump& m_outDump;
		red::Map< CName, Int32 > m_indexMap; // ClassIndex is unavailable at this point
		Int32 m_nextIndex;
	};
}