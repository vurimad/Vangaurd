#pragma once

#include "../../../common/redContainers/include/redContainersPublic.h"
#include "resourcePath.h"

namespace serialization
{
	class FileTables;
	class StructureMapper;

	// Helper class that can be used to build the content of the FileTables
	class FileTablesBuilder
	{
	public:
		explicit FileTablesBuilder( FileTables& outData, const StructureMapper& structureMapper );
		~FileTablesBuilder();

		struct ImportInfo
		{
			res::ResourcePath	m_path;
			CName				m_className;
			Bool				m_isObligatory:1;
			Bool				m_isSoft:1;

			RED_INLINE ImportInfo()
				: m_isObligatory(0)
				, m_isSoft(0)
			{}
		};

		struct ExportInfo
		{
			CName		m_className;
			Uint32		m_parent;

			RED_INLINE ExportInfo()
				: m_parent(0)
			{}
		};

		struct BufferInfo
		{
			Uint32		m_dataSizeInMemory;
			Uint32		m_dataSizeOnDisk;
			Bool		m_hintGPUMemory;
			Bool		m_hintAutoLoadPC = false;
			Bool		m_hintAutoLoadXboxOne = false;
			Bool		m_hintAutoLoadPS4 = false;

			RED_INLINE BufferInfo()
				: m_dataSizeInMemory( 0 )
				, m_dataSizeOnDisk( 0 )
				, m_hintGPUMemory( false )
				, m_hintAutoLoadPC( false )
				, m_hintAutoLoadXboxOne( false )
				, m_hintAutoLoadPS4( false )
			{}
		};

		struct InplaceResourceInfo
		{
			Uint32 m_importIndex = -1;
			Uint32 m_exportIndex = -1 ;
		};

		// add name (no mapping)
		Uint16 AddName( const CName name );

		// add import
		Uint32 AddImport( const ImportInfo& importInfo );

		// add export
		Uint32 AddExport( const ExportInfo& exportInfo );

		// add buffer data
		Uint32 AddBuffer( const BufferInfo& bufferInfo );

		Uint32 AddInplaceResource( const InplaceResourceInfo& inplaceResourceInfo );

		// add ANSI string, returns string index
		Uint32 MapString( const String& string );

		// add mapped name, returns name index
		Uint16 MapName( const CName name ) const;

		// patch export with data offset and size
		void PatchExport( const Uint32 exportIndex, const Uint32 dataOffset, const Uint32 dataSize, const Uint32 dataCRC );

		// patch buffer with data offset and size
		void PatchBuffer( const Uint32 bufferIndex, const Uint32 dataOffset, const Uint32 dataSizeOnDisk, const Uint32 bufferCRC );

	private:
		using TStringMap = red::HashMap< String, Uint32 >;
		using TNameMap = red::HashMap< CName, Uint16 >;

		FileTables*		m_data;
		TStringMap		m_stringMap{ red::PoolEngine() };
		TNameMap		m_nameMap{ red::PoolEngine() };
	};

} // serialization
