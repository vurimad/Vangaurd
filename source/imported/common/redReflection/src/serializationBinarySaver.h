/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "serializationSaver.h"
#include "../../redMemory/include/atomicSharedPtr.h"

namespace serialization
{
	class StructureMapper;
	class FileTablesBuilder;
	struct ProcessedBufferInfo;

	/// Binary saver
	class BinarySaver : public ISaver
	{
	public:
		BinarySaver();

		// Save objects, returns true on success
		virtual Bool SaveObjects( IFile& file, const SavingContext& context ) const override final;

#ifndef NO_EDITOR
		virtual Bool SaveObjects( IFile& file, const SavingContext& context, job::Builder& builder, bool* saveResult ) const override final;
#endif
	private:
		static void SetupNames( StructureMapper& mapper, FileTablesBuilder& builder );
		static void SetupImports( StructureMapper& mapper, FileTablesBuilder& builder );
		static void SetupExports( StructureMapper& mapper, FileTablesBuilder& builder );
		static void SetupBuffers( StructureMapper& mapper, FileTablesBuilder& builder );
		static void SetupInplaceResources( StructureMapper& mapper, FileTablesBuilder& builder );

		Bool WriteExports( IFile& file, const SavingContext& context, const StructureMapper& mapper, FileTablesBuilder& tables, const Uint64 headerOffset ) const;
		Bool WriteBuffers( IFile& file, const SavingContext& context, const StructureMapper& mapper, FileTablesBuilder& tables, const Uint64 headerOffset ) const;
	};

} // serialization
