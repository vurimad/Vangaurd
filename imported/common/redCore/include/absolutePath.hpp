/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace red
{

void AbsolutePath::Clear()
{
	m_path.Clear();
}

bool AbsolutePath::Empty() const
{
	return m_path.Empty();
}

Uint32 AbsolutePath::Length() const
{
	return m_path.Length();
}

const AnsiChar* AbsolutePath::AsChar() const
{
	return m_path.AsChar();
}

const String& AbsolutePath::AsString() const
{
	return m_path;
}

StringView AbsolutePath::AsStringView() const
{
	return m_path;
}

Uint32 AbsolutePath::CalcHash() const
{
	return m_path.CalcHash();
}

const char* AbsolutePath::ToDebugString() const
{
	return m_path.AsChar();
}

template< Uint32 Length >
struct err::CrashDataTypeAdapter<::red::AbsolutePath, Length>
{
	//#tbd: longest allowed path
	static_assert(Length == 0, "Length should already be set for longest path");
	using StorageType = char[MAX_PATH];
	using SetType = ::red::AbsolutePath;

	// Extract the name now, since could lose its refcount, get destructed atexit
	// or some other corruption occurs
	static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
	{
		const Uint32 destSize = RED_ARRAY_COUNT_U32(mem);
		if (!red::Strcpy(mem, value.ToDebugString(), destSize, destSize - 1))
		{
			return CrashDataCopyResult::Error;
		}

		const Uint32 srcLen = value.AsStringView().Length();
		return srcLen + 1 <= destSize ? CrashDataCopyResult::Success : CrashDataCopyResult::Truncated;
	}

	static Bool Print(char* buffer, const Uint32 bufferLen, const StorageType& val)
	{
		return red::Strcpy(buffer, val, bufferLen);
	}
};

} // namespace red
