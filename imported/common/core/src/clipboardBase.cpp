#include "build.h"
#include "clipboardBase.h"

namespace helper
{
	class NullClipboard : public IClipboard
	{
	public:
		virtual Bool Open( EClipboardFormat format ) override { return true; }
		virtual void Close() override {}

		virtual Bool Copy( const void* data, size_t length ) override { return true; }
		virtual Bool Paste( void* data, size_t length ) override { return false; }
		virtual Bool HasData() { return false; }
	};
}

static helper::NullClipboard nullClipboard;
IClipboard* GClipboard = &nullClipboard;
