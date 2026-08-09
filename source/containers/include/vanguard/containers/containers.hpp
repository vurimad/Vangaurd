#pragma once

#include <vanguard/memory/pool.hpp>

// Complete compatibility container image. This is the sole Vanguard public adaptation
// boundary; callers use the Vanguard namespace below.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#pragma warning(disable : 4324)
#endif

#include "../../../../imported/common/redContainers/include/redContainersPublic.h"
#include "../../../../imported/common/redContainers/include/arrayWrapper.h"
#include "../../../../imported/common/redContainers/include/bitSetAtomicLatch.h"
#include "../../../../imported/common/redContainers/include/blob.h"
#include "../../../../imported/common/redContainers/include/circularBuffer.h"
#include "../../../../imported/common/redContainers/include/dynArrayAccessor.h"
#include "../../../../imported/common/redContainers/include/fixedArray.h"
#include "../../../../imported/common/redContainers/include/fixedBuffer.h"
#include "../../../../imported/common/redContainers/include/fundamentalStringConversion.h"
#include "../../../../imported/common/redContainers/include/fundamentalStringParser.h"
#include "../../../../imported/common/redContainers/include/heap.h"
#include "../../../../imported/common/redContainers/include/indexAllocator.h"
#include "../../../../imported/common/redContainers/include/indexRange.h"
#include "../../../../imported/common/redContainers/include/intrusiveList.h"
#include "../../../../imported/common/redContainers/include/lockFreeQueue.h"
#include "../../../../imported/common/redContainers/include/lruIdPool.h"
#include "../../../../imported/common/redContainers/include/minSet.h"
#include "../../../../imported/common/redContainers/include/packedArray.h"
#include "../../../../imported/common/redContainers/include/priQueue.h"
#include "../../../../imported/common/redContainers/include/queue.h"
#include "../../../../imported/common/redContainers/include/staticArrayAccessor.h"
#include "../../../../imported/common/redContainers/include/string/stringBuffer.h"
#include "../../../../imported/common/redContainers/include/string/stringBuilder.h"
#include "../../../../imported/common/redContainers/include/string/stringLocale.h"
#include "../../../../imported/common/redContainers/include/string/stringPrinter.h"
#include "../../../../imported/common/redContainers/include/string/stringUtils.h"
#include "../../../../imported/common/redContainers/include/string/tokenizer.h"
#include "../../../../imported/common/redContainers/include/ustring/ustring.h"
#include "../../../../imported/common/redContainers/include/ustring/ustringUtil.h"
#include "../../../../imported/common/redContainers/include/ustring/utf16String.h"
#include "../../../../imported/common/redContainers/include/ustring/utf8String.h"
#include "../../../../imported/common/redContainers/include/ustring/utfHelpers.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vanguard::containers
{
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    // Arrays, buffers, spans, and blobs.
    using ::red::ArrayConstIterator;
    using ::red::ArrayIterator;
    using ::red::ArrayReverseConstIteration;
    using ::red::ArrayReverseIteration;
    using ::red::ArraySpan;
    using ::red::ArrayWrapper;
    using ::red::ArrayWrapperExt;
    using ::red::Blob;
    using ::red::BlobSpan;
    using ::red::BlobView;
    using ::red::CircularBuffer;
    using ::red::DynamicBuffer;
    using ::red::DynArray;
    using ::red::DynArrayAccessor;
    using ::red::FixedArray;
    using ::red::FixedBuffer;
    using ::red::SortedArray;
    using ::red::StaticArray;
    using ::red::StaticArrayAccessor;

    template <typename T> using DynamicArray = ::red::DynArray<T>;

    // Associative containers and policies.
    using ::red::DefaultHashFunc;
    using ::red::DefaultHashPolicy;
    using ::red::HashMap;
    using ::red::HashPolicy;
    using ::red::HashPolicyDefaultEqual;
    using ::red::HashPolicyDefaultHash;
    using ::red::HashSet;
    using ::red::Map;
    using ::red::Set;

    // Bit containers.
    using ::red::BitSet;
    using ::red::BitSet64;
    using ::red::BitSet64Dynamic;
    using ::red::BitSetAtomicLatch;
    using ::red::BitSetDynamic;
    using ::red::Mask;

    // Queues, heaps, packed storage, pools, and intrusive structures.
    using ::red::CircularArrayConstIterator;
    using ::red::CircularArrayIterator;
    using ::red::Heap;
    using ::red::IntrusiveList;
    using ::red::IntrusiveListNode;
    using ::red::LockFreeQueue;
    using ::red::LockFreeQueueFeintResult;
    using ::red::LRUIdPool;
    using ::red::MPMCLockFreeQueue;
    using ::red::MPSCLockFreeQueue;
    using ::red::PackedArray;
    using ::red::Pool;
    using ::red::PriQueue;
    using ::red::Queue;
    using ::red::SPMCLockFreeQueue;

    using ObjectPool = ::red::Pool;

    // Iteration and operation results.
    using ::red::CheckedConstIterator;
    using ::red::CheckedIterator;
    using ::red::ContainerOpResult;
    using ::red::IndexIterator;
    using ::red::IndexRange;
    using ::red::ReverseIndexIterator;
    using ::red::ReverseIndexRange;

    // String families.
    using ::red::String;
    using ::red::StringBuffer;
    using ::red::StringBuilder;
    using ::red::StringView;
    using ::red::Utf16String;
    using ::red::Utf8String;

    using ::red::Base64Decode;
    using ::red::Base64Encode;
    using ::red::FormatByteNumber;
    using ::red::FuzzyMatch;
    using ::red::IsAllWhiteSpace;
    using ::red::IsIntegerBase10;
    using ::red::StrAppend;
    using ::red::StrCat;
    using ::red::Trim;
    using ::red::TrimLeft;
    using ::red::TrimRight;

    // These two compatibility allocator types remain in the global namespace.
    using ::CAnsiToUnicode;
    using ::CStaticTokenizer;
    using ::CStringPrinter;
    using ::CTokenizer;
    using ::CUnicodeToAnsi;
    using ::FromString;
    using ::IDAllocator;
    using ::IDAllocatorDynamic;
    using ::ToString;
    using ::ToStringDirect;
    using ::red::IndexAllocator;

    namespace algorithms = ::red::alg;
    namespace policies = ::red::policies;

    using ::red::alg::DefaultEqualFunc;
    using ::red::alg::MinSet;
} // namespace vanguard::containers
