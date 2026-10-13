/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "selectionFwd.h"

#include "../../../common/redFileSystem/include/fileFormat.h"
#include "../../../common/redCore/include/absolutePath.h"


namespace tools
{
	class BACKEND_ENGINE_API ISelectionExporter
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( ISelectionExporter );

	public:
		class BACKEND_ENGINE_API ExportOptions
		{
		public:
			ExportOptions( const SelectionStatePtr& selection, const red::AbsolutePath& saveFilePath, const CFileFormat& saveFileFormat );
			~ExportOptions();

			const SelectionStatePtr m_selection;
			const red::AbsolutePath m_saveFilePath;
			const CFileFormat m_saveFileFormat;
		};

		virtual Bool HasSupport( const SelectionStatePtr& selection ) = 0;
		virtual Bool DoExport( const ExportOptions& options ) = 0;
		virtual CFileFormat GetFileFormat() const = 0;

		static ISelectionExporter* FindExporter( const SelectionStatePtr& selection );
	};
}


