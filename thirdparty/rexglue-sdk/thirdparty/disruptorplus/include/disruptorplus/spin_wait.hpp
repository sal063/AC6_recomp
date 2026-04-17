#ifndef DISRUPTORPLUS_SPIN_WAIT_HPP_INCLUDED
#define DISRUPTORPLUS_SPIN_WAIT_HPP_INCLUDED

#include <thread>

#if defined(_M_IX86) || defined(_M_AMD64) || defined(__i386__) || defined(__amd64__)
# define DISRUPTORPLUS_ARCH_X86
# include <immintrin.h>
#endif

#if defined(_M_ARM) || defined(_M_ARM64) || defined(__arm__) || defined(__aarch64__)
# define DISRUPTORPLUS_ARCH_ARM
# ifdef _MSC_VER
#  include <intrin.h>
# endif
#endif

namespace disruptorplus
{
    /// \brief
    /// A helper class for implementing spin-wait loops.
    ///
    /// Call \ref spin_once() each time through the loop
    /// to wait for a short time. Initially just trying to
    /// put the CPU into an idle mode (eg. to allow other
    /// hyper-threads on same core to run) and eventually
    /// yielding the rest of the thread's time-slice or
    /// putting the thread to sleep for a short time.
    ///
    /// For example:
    /// \code
    /// std::atomic<bool>& flag = someSharedFlag;
    /// disruptorplus::spin_wait spinner;
    /// while (!flag.load())
    /// {
    ///    spinner.spin_once();
    /// }
    /// \endcode
    ///
    /// \note
    /// On single-core machines this will not with the CPU
    /// idling phase and will proceed straight to yielding
    /// the remainder of the thread time-slice.
    class spin_wait
    {
    public:
    
        spin_wait()
        {
            reset();
        }
        
        /// \brief
        /// Reset the spin_wait back to its original state.
        void reset()
        {
            m_value = std::thread::hardware_concurrency() > 1 ? 0 : 10;
        }
        
        /// \brief
        /// Wait for a short period of time.
        ///
        /// Call this method each time through a spin-wait loop.
        void spin_once()
        {
            // Exponentially longer sequences of busy-waits each
            // time we're called for first 10 calls, then graduating
            // to yielding our time slice with every 20th call then
            // putting the thread to sleep for a short while.
            if (next_spin_will_yield())
            {
                uint32_t count = m_value - 10;
                if (count % 20 == 19)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            else
            {
                uint32_t count = 4 << m_value;
                while (count-- != 0)
                {
                    yield_processor();
                }
            }
            m_value = (m_value == 0xFFFFFFFF) ? 10 : m_value + 1;
        }
        
        /// \brief
        /// Query whether the next call to \ref spin_once() will yield the
        /// remainder of the thread's time slice.
        ///
        /// Call this if you want to perform some alternative logic prior
        /// to the thread being rescheduled.
        ///
        /// \return
        /// \c true if the next call to \ref spin_once() will yield the
        /// remainder of the thread's time slice, \c false otherwise.
        bool next_spin_will_yield() const
        {
            return m_value >= 10;
        }
    
    private:

        /// \brief
        /// Indicate to the CPU that the current thread is waiting and
        /// so should be put in an idle mode.
        ///
        /// eg. On Intel processors with HyperThreading this may signal
        /// to allow the other thread running on this core to execute.
        static void yield_processor()
        {
            // Ideally we want to put this processor into idle mode for a few cycles.
#if defined(DISRUPTORPLUS_ARCH_X86)
            _mm_pause();
#elif defined(DISRUPTORPLUS_ARCH_ARM)
# if defined(_MSC_VER)
            __yield();
# else
            __asm__ volatile("yield" ::: "memory");
# endif
#endif
        }
    
        uint32_t m_value;
    
    };
}

#endif
