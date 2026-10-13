/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "selectionExporter.h"
#include "selectionState.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( ISelectionExporter, tools );
RTTI_END_TYPE();

namespace tools
{
	ISelectionExporter* ISelectionExporter::FindExporter( const SelectionStatePtr& selection )
	{
		red::DynArray< const rtti::ClassType* > exporterClasses;
		GetRttiSystem().EnumClasses( ClassID< ISelectionExporter >(), exporterClasses );

		for ( const auto& exportClass: exporterClasses )
		{
			ISelectionExporter* exporter = exportClass->GetDefaultObject< ISelectionExporter >();
			if ( exporter->HasSupport( selection ) )
			{
				return exporter;
			}
		}

		return nullptr;
	}

	ISelectionExporter::ExportOptions::ExportOptions( const SelectionStatePtr& selection, const red::AbsolutePath& saveFilePath, const CFileFormat& saveFileFormat )
		: m_selection( selection ),
		m_saveFilePath( saveFilePath ),
		m_saveFileFormat( saveFileFormat)
	{
	}

	ISelectionExporter::ExportOptions::~ExportOptions() = default;

}
