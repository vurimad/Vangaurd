/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "worldGlobalNodeIDUtils.h"

#include "../../redContainers/include/string/stringUtils.h"

namespace red
{
	namespace globalid
	{
		Bool IsRelativeGlobalIDPath( const red::StringView path )
		{
			return path.StartsWith( "~/" );
		}

		namespace
		{
			static constexpr char c_pathSeparators[] = "/\\";

			Uint32 CountPathElements( const red::StringView path )
			{
				if ( path.Empty() )
				{
					return 0;                // Do we care about pure whitespace? For now whitespace character acts as regular path element character.
				}

				Uint32 numPathElements = 1;
				Uint32 separatorPos = path.FindAnyOf( c_pathSeparators );
				while ( separatorPos != red::StringView::npos )
				{
					separatorPos = path.FindAnyOf( c_pathSeparators, separatorPos + 1 );
					++numPathElements;
				}

				return numPathElements;
			}
		}

		red::String MakeRelativeDeltaPath( const red::StringView refPath, const red::StringView destPath )
		{
			Bool pathEnded1 = false;
			Bool pathEnded2 = false;
			Uint32 offset = 0;
			Uint32 numMatchingPathElements = 0;
			red::StringView restOfTheRefPath = refPath;
			red::StringView restOfTheDestPath = destPath;

			while ( !(pathEnded1 || pathEnded2) )
			{
				Uint32 nextSeparatorPos1 = refPath.FindAnyOf( c_pathSeparators, offset );
				Uint32 nextSeparatorPos2 = destPath.FindAnyOf( c_pathSeparators, offset );
				pathEnded1 = (nextSeparatorPos1 == red::StringView::npos);
				pathEnded2 = (nextSeparatorPos2 == red::StringView::npos);
				Uint32 nextElementEndPos1 = pathEnded1 ? refPath.Length() : nextSeparatorPos1;
				Uint32 nextElementEndPos2 = pathEnded2 ? destPath.Length() : nextSeparatorPos2;
				if ( (nextElementEndPos1 != nextElementEndPos2) || (refPath.Slice( offset, nextElementEndPos1 ) != destPath.Slice( offset, nextElementEndPos2 )) )
				{
					break;
				}

				restOfTheRefPath = refPath.SubView( nextElementEndPos1 + !pathEnded1 );
				restOfTheDestPath = destPath.SubView( nextElementEndPos2 + !pathEnded2 );

				offset = nextElementEndPos1 + 1u;          // +1 to skip separator character itself.
				++numMatchingPathElements;
			}

			if ( numMatchingPathElements == 0 )              // Do we want to make an exception for "$" for first path element? For now we don't.
				return destPath.ToString();

			String result;
			Uint32 numRefPathElementsLeft = CountPathElements( restOfTheRefPath );

			if ( (numRefPathElementsLeft == 0) && restOfTheDestPath.Empty() )
			{
				// Reference and destination paths are identical (might differ in separators).
				// We treat this case specially to still return relative path, going one level up and back.
				// Returning empty string is a bad choice here, so is returning absolute path.
				Uint32 lastSeparatorPos = destPath.FindAnyOfReverse( c_pathSeparators );
				StringView lastElementOfTheDestPath = (lastSeparatorPos != StringView::npos) ? destPath.SubView( lastSeparatorPos + 1u ) : destPath;

				result = "../";
				result.Append( lastElementOfTheDestPath.Data(), lastElementOfTheDestPath.Length() );
				return result;
			}

			result.Reserve( 3u * numRefPathElementsLeft + restOfTheDestPath.Length() );

			for ( Uint32 i = 1; i < numRefPathElementsLeft; ++i )
			{
				result += "../";
			}

			if ( numRefPathElementsLeft > 0 )
			{
				result += "..";
				if ( !restOfTheDestPath.Empty() )       // Last path separator is added only when we actually append any path after it.
				{
					result += '/';
				}
			}

			result.Append( restOfTheDestPath.Data(), restOfTheDestPath.Length() );
			return result;
		}

		red::String MakeRelativeDeltaGlobalIDPath( const red::StringView referencePath, const red::StringView destPath )
		{
			red::String result = MakeRelativeDeltaPath( referencePath, destPath );
			if ( result != destPath )
				result = "~/" + result;
			return result;
		}

	} // globalid

} // red
