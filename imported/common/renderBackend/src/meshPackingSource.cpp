/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "meshPackingSource.h"

using namespace tools;

//------------------

MeshPackingSource::MeshPackingSource()
{
}

MeshPackingSource::~MeshPackingSource()
{
	m_chunks.Clear();
	m_lods.Clear();
}

MeshPackingSource::PackedStreams::PackedStreams()
{
	red::Memzero( &m_vertices, sizeof(m_vertices) );
	m_indices = 0;
	m_topologyData = 0;
	m_topologyMetadata = 0;
}

MeshPackingSource::PackedStreams::~PackedStreams()
{
}
