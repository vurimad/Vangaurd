#pragma once

// RED Vanguard owns build identity at the system layer. This avoids the
// original reverse dependency from redSystem into redReflection.
#define APP_VERSION_MAJOR "0"
#define APP_VERSION_MINOR "1"
#define APP_VERSION_BUILD "local"
#define APP_LAST_P4_CHANGE "RED_VANGUARD"
#define APP_P4_STREAM "main"
#define APP_P4_SHELF ""
#define APP_VERSION_NUMBER APP_VERSION_MAJOR "." APP_VERSION_MINOR "." APP_VERSION_BUILD \
	"  Revision: " APP_LAST_P4_CHANGE "  Stream: " APP_P4_STREAM
#define APP_DATE __DATE__
