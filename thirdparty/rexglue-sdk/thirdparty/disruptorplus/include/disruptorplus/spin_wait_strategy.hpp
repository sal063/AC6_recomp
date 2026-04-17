#ifndef DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED

#include "spin_wait.hpp"
#include "sequence.hpp"

#include <chrono>
#include <atomic>
#include <cassert>

namespace disruptorplus
{
    /// \brief
    /// This wait-strategy busy-waits when waiting for sequences to be
    /// published.
    ///
    /// The busy wait used is a phased wait which backs off incrementally,
    /// initially actively busy waiting, falling back to yielding the
    /// thread's remaining time-slice and sleeping occasionally.
    /// This attempts to balance low-latency throughput during busy periods
    /// with low CPU usage during the quiet periods.
    ///
    /// \see disruptorplus::spin_wait
    /// \see disruptorplus::blocking_wait_strategy
    class spin_wait_strategy
    {
    public:
    
        /// \brief
        /// Wait unconditionally until all of the specified sequences
        /// have at least published the specified sequence value.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param count
        /// The number of elements in \p sequences.
        /// Must be greater than zero.
        ///
        /// \param sequences
        /// An array of \p count pointers to sequence values.
        /// The call will not return until all of these values have advanced
        /// beyond the specified \p sequence.
        ///
        /// \return
        /// The value of the least-advanced sequence.
        /// This value is guaranteed to be at least \p sequence.
        ///
        /// \throw std::system_error
        /// If the system does not have enough resources to perform this operation.
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[])
        {
            assert(count > 0);
            spin_wait spinner;
            sequence_t result = minimum_sequence_after(sequence, count, sequences);
            while (difference(result, sequence) < 0)
            {
                spinner.spin_once();
                result = minimum_sequence_after(sequence, count, sequences);
            }
            return result;
        }

        /// \brief
        /// Wait until either all of the specified sequences have at least
        /// published the specified sequence value or a timeout is reached.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param count
        /// The number of elements in \p sequences.
        /// Must be greater than zero.
        ///
        /// \param sequences
        /// An array of \p count pointers to sequence values.
        /// The call will wait until all of these sequence values have
        /// published a sequence number at or after the specified
        /// \p sequence number.
        ///
        /// \param timeout
        /// The maximum amount of time to wait for the \p sequences
        /// numbers to advance to \p sequence.
        ///
        /// \return
        /// If the operation timed out then returns some number such
        /// that <tt>difference(result, sequence) < 0</tt>, otherwise
        /// returns the least-advanced of all the sequence values read
        /// from \p sequences, which is guaranteed to satisfy
        /// <tt>difference(result, sequence) >= 0</tt>.
        ///
        /// \throw std::system_error
        /// If the system does not have enough resources to perform this operation.
        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[],
            const std::chrono::duration<Rep, Period>& timeout)
        {
            return wait_until_published(
                sequence,
                count,
                sequences,
                std::chrono::high_resolution_clock::now() + timeout);
        }

        /// \brief
        /// Wait until either all of the specified sequences have at least
        /// published the specified sequence value or a timeout time is reached.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param count
        /// The number of elements in \p sequences.
        /// Must be greater than zero.
        ///
        /// \param sequences
        /// An array of \p count pointers to sequence values.
        /// The call will wait until all of these sequence values have
        /// published a sequence number at or after the specified
        /// \p sequence number.
        ///
        /// \param timeoutTime
        /// The time to wait until for the \p sequences numbers to
        /// advance to \p sequence. The operation will timeout after
        /// this point in time.
        ///
        /// \return
        /// If the operation timed out then returns some number such
        /// that <tt>difference(result, sequence) < 0</tt>, otherwise
        /// returns the least-advanced of all the sequence values read
        /// from \p sequences, which is guaranteed to satisfy
        /// <tt>difference(result, sequence) >= 0</tt>.
        ///
        /// \throw std::system_error
        /// If the system does not have enough resources to perform this operation.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[],
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            assert(count > 0);
            spin_wait spinner;
            sequence_t result = minimum_sequence_after(sequence, count, sequences);
            while (difference(result, sequence) < 0)
            {
                if (spinner.next_spin_will_yield() && timeoutTime < Clock::now())
                {
                    // Out of time.
                    return result;
                }
                spinner.spin_once();
                result = minimum_sequence_after(sequence, count, sequences);
            }
            return result;
        }

        /// \brief
        /// Notify any waiting threads that one of the sequence values has changed.
        void signal_all_when_blocking()
        {
            // This is requred as part of the wait_strategy interface but does nothing
            // for the spin_wait_strategy since all waiting threads are continuously
            // checking the sequence values in a spin-wait loop.
        }
    
    };
}

#endif
