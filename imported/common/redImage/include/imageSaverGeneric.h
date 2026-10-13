/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "imageSaver.h"

#include "../../redContainers/include/dynArray.h"
#include "../../../common/redFileSystem/include/file.h"

namespace img
{
		struct REDIMAGE_API GenericWriter : public IFile // BaseImageSaver::Writer
		{
		public:
			GenericWriter();

			void Reserve(Uint64 size);
			RED_INLINE Uint8* GetData() { return m_data.TypedData(); }
			virtual Uint64 GetOffset() const override;
			virtual Uint64 GetSize() const override;
			virtual void Seek( Int64 offset ) override;
			virtual void Serialize( /*const*/ void* data, size_t size ) override;
			virtual void Flush() override {}

		private:
			Uint64					m_pos;
			red::DynArray<Uint8>	m_data{ red::PoolBackend() };
		};
} // img
