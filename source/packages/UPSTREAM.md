# RED archive adaptation record

Inspected source image:

- `D:/root/R6.Root/Mainline/dev/src/common/archive/include/common.h`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/include/instance.h`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/include/fileTable.h`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/include/fileMetadata.h`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/src/fileFormat.h`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/src/instance.cpp`
- `D:/root/R6.Root/Mainline/dev/src/common/archive/src/fileTable.cpp`
- `D:/root/R6.Root/Mainline/dev/src/backend/backendDataBuild/src/archivesResourceDistribution.cpp`

Adapted architectural behavior:

- write payloads before the index and commit the header last;
- split a logical resource into independently aligned stored segments;
- retain dependency ranges in the package index;
- keep entries sorted by stable resource identifier;
- validate index integrity before trusting counts and ranges;
- separate runtime read-only access from package building;
- allow package ordering to override an earlier resource.

Deliberately replaced:

- `RADR` magic and archive format version 12;
- structure-dump wire serialization;
- RED `ResourcePath` hashes and 20-byte archive content hashes;
- RED compression wrappers and padding byte;
- archive debug machine/build records;
- RED production directory assumptions and fixed archive names;
- compile-time final-build removal of writer declarations.

Vanguard's writer remains available to tools in every configuration, while
runtime applications choose whether to link tool-side construction.

Raw LZ4 coding calls the LZ4 implementation already carried by the imported
compression third-party image. It does not call RED's compression wrapper and
does not emit its headers.
