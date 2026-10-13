#include "build.h"
#include "memoryBudgets.h"
#include "stringConversion.h"
#include "../../resource/include/2darray.h"
#include "../../redCore/include/absolutePath.h"
#include "../../redMemory/include/poolUtils.h"
#include "../../redFileSystem/include/fileSys.h"

/*
// Commenting out for now, because gamers believe this config is the true source for hidden performance ;>

 namespace
{
	const char* c_memory_pool_budgetsFilePath = "config\\memory_pool_budgets.csv";
	const char* c_invalidPool = "-1";
	const char* c_dynamicPoolBudget = "0";

	void LoadPoolBudgets()
	{
		const red::AbsolutePath configFilePath = GFileManager->GetEngineRoot().AddFilePath( c_memory_pool_budgetsFilePath );
		red::String csvContent;
		if ( !red::LoadFileToString( configFilePath, csvContent ) )
		{
			RED_LOG_ERROR( "Failed to read memory budgets from CSV file: %s.", configFilePath.AsChar() );
			return;
		}

		C2dArray configCsv;
		configCsv.ParseData( std::move( csvContent) );
		for ( Uint32 i = 0; i < configCsv.GetNumberOfRows(); ++i )
		{
			const red::String poolName = configCsv.GetValue( 0, i ).TrimCopy();
			if ( poolName.Empty() )
			{
				continue;
			}

			red::String budget = 
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
				configCsv.GetValue( 1, i ).TrimCopy();
#elif defined( RED_PLATFORM_DURANGO )
				configCsv.GetValue( 2, i ).TrimCopy();
#elif defined( RED_PLATFORM_ORBIS )
				configCsv.GetValue( 3, i ).TrimCopy();
#endif

			if ( budget.Empty() )
			{
				// pool without budget (pure virtual pool like PoolRoot)
				continue;
			}

			if ( budget == c_invalidPool )
			{
				// pool does not exist on current platform
				continue;
			}

			if ( budget == c_dynamicPoolBudget )
			{
				// budget will be computed dynamically
				continue;
			}

			const auto parseBudget = []( const red::String& value ) {
				const auto getBudgetValue = []( const red::String& value, const red::String& suffix = red::String::EMPTY() ) {
					red::String valueString = suffix != red::String::EMPTY() ? value.StringBefore( suffix ) : value;
					RED_ASSERT( std::all_of( valueString.Begin(), valueString.End(), ::isdigit ), "Invalid memory budget was given." );
					return FromStringDirect< Int64 >( valueString );
				};

				if ( value.EndsWith( "KB" ) )
					return static_cast< Int64 >( RED_KILO_BYTE( getBudgetValue( value, "KB" ) ) );
				else if ( value.EndsWith( "MB" ) )
					return static_cast< Int64 >( RED_MEGA_BYTE( getBudgetValue( value, "MB" ) ) );
				else if ( value.EndsWith( "GB" ) )
					return static_cast< Int64 >( RED_GIGA_BYTE( getBudgetValue( value, "GB" ) ) );
				else
					return value.EndsWith( "B" ) ? getBudgetValue( value, "B" ) : getBudgetValue( value );
			};

			const Int64 pcBudgetValue = parseBudget( budget );

			RED_SET_MEMORY_POOL_BUDGET( poolName.AsChar(), pcBudgetValue );
		}
	}
}
*/

namespace red
{
	void SetMemoryBudgets()
	{
		//LoadPoolBudgets();
		red::memory::ValidateAllPoolBudget();
	}
}