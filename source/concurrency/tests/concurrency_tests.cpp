#include <vanguard/concurrency/concurrency.hpp>

namespace
{
    class AtomicWorker final : public vanguard::concurrency::Thread
    {
    public:
        explicit AtomicWorker(vanguard::concurrency::Atomic<vanguard::u32>& counter) : Thread("atomicWorker"), m_counter(counter) {}

        void ThreadFunction() noexcept override
        {
            for (vanguard::u32 index = 0; index < 100000; ++index)
            {
                static_cast<void>(m_counter.Increment());
            }
        }

    private:
        vanguard::concurrency::Atomic<vanguard::u32>& m_counter;
    };

    class EventWorker final : public vanguard::concurrency::Thread
    {
    public:
        EventWorker(vanguard::concurrency::ManualResetEvent& event, vanguard::concurrency::Atomic<bool>& completed)
            : Thread("eventWorker"), m_event(event), m_completed(completed)
        {
        }

        void ThreadFunction() noexcept override
        {
            m_event.Wait();
            m_completed.SetValue(true);
        }

    private:
        vanguard::concurrency::ManualResetEvent& m_event;
        vanguard::concurrency::Atomic<bool>& m_completed;
    };
} // namespace

int main()
{
    namespace concurrency = vanguard::concurrency;

    concurrency::InitializeMainThread();
    if (!concurrency::IsMainThread() || !concurrency::ThreadId::GetCurrentThread().IsValid() || concurrency::GetMaxHardwareConcurrency() == 0)
    {
        return 1;
    }

    concurrency::Atomic<vanguard::u32> counter;
    AtomicWorker workers[] = {AtomicWorker(counter), AtomicWorker(counter), AtomicWorker(counter), AtomicWorker(counter)};

    for (AtomicWorker& worker : workers)
    {
        worker.InitThread();
    }
    for (AtomicWorker& worker : workers)
    {
        worker.JoinThread();
    }

    if (counter.GetValue() != 400000)
    {
        return 2;
    }

    concurrency::Mutex mutex;
    mutex.Acquire();
    if (!mutex.TryAcquire())
    {
        return 3;
    }
    mutex.Release();
    mutex.Release();

    concurrency::RWSpinLock rwSpinLock;
    rwSpinLock.AcquireShared();
    rwSpinLock.ReleaseShared();
    if (!rwSpinLock.TryAcquire())
    {
        return 4;
    }
    rwSpinLock.Release();

    concurrency::Semaphore semaphore(0, 1);
    if (semaphore.TryAcquire())
    {
        return 5;
    }
    semaphore.Release();
    if (!semaphore.TryAcquire(10))
    {
        return 6;
    }

    concurrency::ManualResetEvent event;
    concurrency::Atomic<bool> completed;
    EventWorker eventWorker(event, completed);
    eventWorker.InitThread();
    concurrency::SleepOnCurrentThread(1);
    event.Signal();
    eventWorker.JoinThread();

    if (!completed.GetValue() || !event.TryWait())
    {
        return 7;
    }

    event.Reset();
    if (event.TryWait())
    {
        return 8;
    }

    return 0;
}
