#ifndef DISRUPTORPLUS_SINGLE_THREADED_CLAIM_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_SINGLE_THREADED_CLAIM_STRATEGY_HPP_INCLUDED

#include "sequence_range.hpp"
#include "sequence_barrier.hpp"
#include "sequence_barrier_group.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cassert>

namespace disruptorplus
{
    /// \brief
    /// A claim strategy for use where only a single thread will be publishing
    /// items to a ring buffer.
    ///
    /// This strategy avoids the overhead required to synchronise multiple
    /// threads trying to claim slots in the ring buffer.
    ///
    /// A writer thread starts by first claiming one or more slots in the ring buffer
    /// by calling one of the 'claim' methods. The writer then writes to the slots
    /// and indicates to readers it has finished writing to the slots by calling
    /// publish().
    ///
    /// Reader threads can wait until a writer thread has published an element
    /// to the ring buffer by calling one of the wait_until_published() methods.
    ///
    /// Readers indicate they are finished reading from a slot in the ring buffer
    /// by publishing the sequence number of the latest item they are finished
    /// with in the 'claim barrier'. This frees the slot up for writers to
    /// subsequently claim it for future writes.
    ///
    /// \tparam WaitStrategy
    /// The strategy to use to block threads waiting for sequence numbers to be published.
    template<typename WaitStrategy>
    class single_threaded_claim_strategy
    {
    public:
    
        /// \brief
        /// Initialise the claim strategy.
        ///
        /// \param bufferSize
        /// The size of the ring buffer in which elements are allocated.
        /// Must be a power-of-two.
        ///
        /// \param waitStrategy
        /// The wait strategy object to use for blocking threads waiting for
        /// a particular sequence number to be published.
        /// This must be the same object that sequence barriers used by readers
        /// are constructed with.
        single_threaded_claim_strategy(
            size_t bufferSize,
            WaitStrategy& waitStrategy)
        : m_bufferSize(bufferSize)
        , m_nextSequenceToClaim(0)
        , m_claimBarrier(waitStrategy)
        , m_readBarrier(waitStrategy)
        {
            assert(bufferSize > 0 && (bufferSize & (bufferSize - 1)) == 0);
        }

        /// \brief
        /// The number of elements in the ring buffer.
        ///
        /// This will always be a power-of-two
        size_t buffer_size() const
        {
            return m_bufferSize;
        }
        
        /// \brief
        /// Add a sequence barrier for claiming slots in the ring buffer.
        ///
        /// Add a sequence barrier that prevents elements of the ring buffer from
        /// being claimed by writers until the reader has indicated the slot is
        /// available by publishing the sequence number they have finished reading.
        /// Claimed slots will never advance more than buffer_size() ahead
        /// of any of the registered claim barriers.
        ///
        /// \param barrier
        /// The sequence barrier to add.
        /// A reference to the sequence barrier is held by the claim strategy
        /// so the caller must ensure the lifetime of the sequence barrier
        /// exceeds that of the cliam strategy.
        /// This barrier must have been constructed with the same wait strategy object
        /// as the claim strategy was constructed with.
        /// \note
        /// This operation is not thread-safe and the caller must ensure that no other
        /// threads are accessing the claim strategy concurrently with this call.
        void add_claim_barrier(sequence_barrier<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
        }
        
        /// \brief
        /// Add a sequence barrier group for claiming slots in the ring buffer.
        ///
        /// Adds sequence barriers that prevent elements of the ring buffer from
        /// being claimed by writers until the readers have indicated the slot is
        /// available by publishing the sequence number they have finished reading.
        /// Claimed slots will never advance more than buffer_size() ahead
        /// of any of the registered claim barriers.
        ///
        /// \param barrier
        /// The sequence barriers to add.
        /// A reference to each sequence barrier in the group is held by the claim
        /// strategy, so the caller must ensure the lifetime of the sequence barriers
        /// exceed that of the cliam strategy.
        /// This barrier must have been constructed with the same wait strategy object
        /// as the claim strategy was constructed with.
        ///
        /// \note
        /// This operation is not thread-safe and the caller must ensure that no other
        /// threads are accessing the claim strategy concurrently with this call.
        void add_claim_barrier(sequence_barrier_group<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
        }

        /// \brief
        /// Claim a single slot in the ring buffer for writing to.
        ///
        /// Blocks the caller until a slot is available.
        ///
        /// The caller must call \ref publish(sequence_t) once it has
        /// finished writing to the slot to publish the item to readers.
        ///
        /// \return
        /// The sequence number of the slot claimed.
        sequence_t claim_one()
        {
            m_claimBarrier.wait_until_published(
                static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize));
            return m_nextSequenceToClaim++;
        }
        
        /// Claim up to \p count consecutive slots in the ring buffer.
        ///
        /// Blocks the caller until at least one slot is available to claim.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param count
        /// The maximum number of slots to claim.
        ///
        /// \return
        /// A sequence range indicating the sequence numbers that were claimed.
        /// This sequence range may contain less items than requested if fewer
        /// were available but will contain at least one slot if \p count is non-zero.
        sequence_range claim(size_t count)
        {
            sequence_t claimable = static_cast<sequence_t>(
                m_claimBarrier.wait_until_published(
                    static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize)) +
                m_bufferSize);
                
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            assert(diff >= 0);
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            sequence_range result(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            
            return result;
        }
        
        /// \brief
        /// Attempt to claim up to \p count slots in the ring buffer without blocking.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param count
        /// The maximum number of slots to claim.
        ///
        /// \param range [out]
        /// If any slots were claimed then this variable is populated with the
        /// range of sequence numbers that were claimed. If no slots were claimed
        /// then the value is left unchanged.
        ///
        /// \return
        /// Returns \c true if any elements were claimed in which case the range of
        /// slots claimed is written to the \p range out-parameter. May claim less
        /// slots than requested if fewer slots were available.
        /// Returns \c false if no elements were claimed. The \p range out-parameter is
        /// not modified in this case.
        bool try_claim(size_t count, sequence_range& range)
        {
            sequence_t seq = static_cast<sequence_t>(m_claimBarrier.last_published() + m_bufferSize);
            sequence_diff_t diff = difference(seq, m_nextSequenceToClaim);
            if (diff < 0)
            {
                return false;
            }
            assert(diff >= 0);
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            return true;
        }
        
        /// \brief
        /// Attempt to claim up to \p count slots in the ring buffer.
        ///
        /// Blocks the caller until either at least one slot was claimed or until
        /// the specified timeout period has elapsed, whichever comes first.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param count
        /// The maximum number of slots to claim.
        ///
        /// \param range [out]
        /// Receives the range of sequence numbers claimed if the operation succeeds.
        /// Value is left unchanged if no slots were claimed.
        ///
        /// \param timeout
        /// The maximum time to wait for claiming any slots.
        ///
        /// \return
        /// Returns \c true if any slots were claimed in which case the range
        /// of slots claimed is written to the \p range out-parameter.
        /// Returns \c false if the timeout duration was exceeded without
        /// claiming any slots.
        ///
        /// \throw std::exception
        /// May throw any exception thrown by WaitStrategy::wait_until_published().
        template<class Rep, class Period>
        bool try_claim_for(
            size_t count,
            sequence_range& range,
            const std::chrono::duration<Rep, Period>& timeout)
        {
            if (try_claim(count, range))
            {
                return true;
            }
            
            sequence_t claimable =
                static_cast<sequence_t>(
                    m_claimBarrier.wait_until_published(
                        static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize),
                        timeout) + m_bufferSize);
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            if (diff < 0)
            {
                // Timeout
                return false;
            }
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            
            return true;
        }

        /// \brief
        /// Attempt to claim up to \p count slots in the ring buffer.
        ///
        /// Blocks the caller until either at least one slot was claimed or until
        /// the specified timeout period has elapsed, whichever comes first.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param count
        /// The maximum number of slots to claim.
        ///
        /// \param range [out]
        /// Receives the range of sequence numbers claimed if the operation succeeds.
        /// Value is left unchanged if no slots were claimed.
        ///
        /// \param timeoutTime
        /// The time after which the call should stop waiting for available slots.
        ///
        /// \return
        /// Returns \c true if any slots were claimed in which case the range
        /// of slots claimed is written to the \p range out-parameter.
        /// Returns \c false if the timeout duration was exceeded without
        /// claiming any slots.
        ///
        /// \throw std::exception
        /// May throw any exception thrown by \c Clock::now() or
        /// \c WaitStrategy::wait_until_published().
        template<class Clock, class Duration>
        bool try_claim_until(
            size_t count,
            sequence_range& range,
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            if (try_claim(count, range))
            {
                return true;
            }
            
            sequence_t claimable =
                static_cast<sequence_t>(
                    m_claimBarrier.wait_until_published(
                        static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize),
                        timeoutTime) + m_bufferSize);
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            if (diff < 0)
            {
                // Timeout
                return false;
            }
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            
            return true;
        }
        
        /// \brief
        /// Publishes all sequences up to and including \p sequence.
        ///
        /// Notifies reader threads that these items are now available
        /// to be read.
        ///
        /// This operation has 'release' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to publish to readers.
        void publish(sequence_t sequence)
        {
            m_readBarrier.publish(sequence);
        }

        /// \brief
        /// Query the last sequence that was published.
        ///
        /// All sequences up to and including returned sequence value
        /// have been published and are available for readers to access.
        ///
        /// This operation has 'acquire' memory semantics.
        sequence_t last_published() const
        {
            return m_readBarrier.last_published();
        }

        /// \brief
        /// Block the caller until the specified \p sequence has been
        /// published by the writer thread.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be in
        /// advance of the requested sequence.
        sequence_t wait_until_published(sequence_t sequence) const
        {
            return m_readBarrier.wait_until_published(sequence);
        }

        /// \brief
        /// Block the caller until the specified \p sequence has been
        /// published by the writer thread.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param lastKnownPublished
        /// This sequence number is assumed to have already been published.
        /// The initial value passed in here on first call should be sequence_t(-1).
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be in
        /// advance of the requested sequence.
        sequence_t wait_until_published(sequence_t sequence, sequence_t lastKnownPublished) const
        {
            return wait_until_published(sequence);
        }

        /// \brief
        /// Block the caller until either the specified sequence has been
        /// published by the writer thread or until the specified timeout
        /// has elapsed.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeout
        /// The maximum amount of time to wait for the sequence number to
        /// be published.
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be prior to
        /// the specified sequence in the case of a timeout or equal to
        /// or after the specified sequence in the case of that item being
        /// published.
        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            return m_readBarrier.wait_until_published(sequence, timeout);
        }

        /// \brief
        /// Block the caller until either the specified sequence has been
        /// published by the writer thread or until the specified timeout
        /// has elapsed.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param lastKnownPublished
        /// This sequence number is assumed to have already been published.
        /// The initial value passed in here on first call should be sequence_t(-1).
        ///
        /// \param timeout
        /// The maximum amount of time to wait for the sequence number to
        /// be published.
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be prior to
        /// the specified sequence in the case of a timeout or equal to
        /// or after the specified sequence in the case of that item being
        /// published.
        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            sequence_t lastKnownPublished,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            return wait_until_published(sequence, timeout);
        }

        /// \brief
        /// Block the caller until either the specified sequence has been
        /// published by the writer thread or until the specified timeout
        /// time has passed.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeoutTime
        /// The time after which the call should return in failure if
        /// the specified \p sequence has not yet been published.
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be prior to
        /// the specified sequence in the case of a timeout or equal to
        /// or after the specified sequence in the case of that item being
        /// published.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            return m_readBarrier.wait_until_published(sequence, timeoutTime);
        }

        /// \brief
        /// Block the caller until either the specified sequence has been
        /// published by the writer thread or until the specified timeout
        /// has elapsed.
        ///
        /// This operation has 'acquire' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param lastKnownPublished
        /// This sequence number is assumed to have already been published.
        /// The initial value passed in here on first call should be sequence_t(-1).
        ///
        /// \param timeoutTime
        /// The time after which the call should return in failure if
        /// the specified \p sequence has not yet been published.
        ///
        /// \return
        /// Returns the value of \ref last_published() which may be prior to
        /// the specified sequence in the case of a timeout or equal to
        /// or after the specified sequence in the case of that item being
        /// published.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            sequence_t lastKnownPublished,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            return wait_until_published(sequence, timeoutTime);
        }

    private:
    
        const size_t m_bufferSize;
        
        // The next sequence to be claimed (may not yet be available).
        sequence_t m_nextSequenceToClaim;
        
        // A cache of the last-known available sequence value
        // queried using m_claimBarrier.
        sequence_t m_lastKnownClaimableSequence;

        sequence_barrier_group<WaitStrategy> m_claimBarrier;
        
        // Barrier used to publish items to the 
        sequence_barrier<WaitStrategy> m_readBarrier;
    
    };
    
}

#endif
