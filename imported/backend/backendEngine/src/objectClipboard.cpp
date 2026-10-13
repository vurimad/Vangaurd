#include "build.h"
#include "objectClipboard.h"

namespace tools
{

	IObjectClipboard::IObjectClipboard()
	{
	}

	IObjectClipboard::~IObjectClipboard()
	{
	}

	void IObjectClipboard::SetupInterop( IObjectClipboard* ptr, ::interop::utils::Object& op )
	{
		// nothing
	}

} // tools