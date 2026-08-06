project "redLexer"
	kind "StaticLib"
	language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"
	location "../../../../build/%{_ACTION}/projects/redLexer"

	includedirs
	{
		"include",
		"src",
		"gen"
	}

	files
	{
		"include/**.h",
		"src/build.h",
		"src/flexSupplimentary.h",
		"src/lexer.cpp",
		"src/state.cpp",
		"src/scripts.flex",
		"gen/bison_tokens.h",
		"grammar/scriptFileParser.bison"
	}

	filter "system:windows"
		defines { "_WINDOWS" }

	filter {}
