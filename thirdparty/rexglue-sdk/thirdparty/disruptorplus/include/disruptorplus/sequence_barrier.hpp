#ifndef DISRUPTORPLUS_SEQUENCE_BARRIER_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_BARRIER_HPP_INCLUDED

#include "config.hpp"
#include "sequence.hpp"

#include <atomic>
#include <chrono>

namespace disruptorplus
{
    template<typename WaitStrategy>
    class sequence_barrier_group;

    /// \brief
    /// A sequence barrier holds a sequence number that can be used to
    /// publish which item has finished processing and is now available.
    ///
    /// The assumption is that when a sequence number is published in a
    /// sequence barrier that the specified sequence number and all prior
    /// sequence numbers are available for down-stream threads to consume.
    ///
    /// While it is only safe for a single thread to publish sequence numbers,
    /// an arbitrary number of threads can wait for a sequence barrier to
    /// reach a desired sequence number.
    ///
    /// \tparam WaitStrategy
    /// A class that defines the strategy to use for blocking threads while
    /// waiting for a given sequence number to be published.
    /// Must implement the wait_strategy model.
    template<typename WaitStrategy>
    class sequence_barrier
    {
    public:
    
        /// \brief
        /// Initialise the sequence barrier.
        ///
        /// The sequence barrier's initial published sequence number is the
        /// number immediately preceding zero. ie. the next sequence number
        /// to be published is zero.
        ///
        /// \param waitStrategy
        /// The wait strategy to use for threads waiting on this sequence barrier.
        /// The constructed sequence barrier will hold a reference to this object
        /// so callers must ensure the lifetime of the wait strategy exceeds that
        /// of the sequence barrier.
        sequence_barrier(WaitStrategy& waitStrategy)
        : m_waitStrategy(waitStrategy)
        , m_lastPublished(static_cast<sequence_t>(-1))
        {}
        
        /// \brief
        /// Query the sequence number last published to this sequence barrier.
        ///
        /// This operation synchronises with the corresponding call to publish()
        /// and has 'acquire' memory semantics.
        ///
        /// \return
        /// The last-published sequence number.
        sequence_t last_published() const
        {
            return m_lastPublished.load(std::memory_order_acquire);
        }
        
        /// \brief
        /// Block the calling thread until the specified sequence number is published.
        ///
        /// This operation synchronises with the corresponding call to publish()
        /// that publishes the returned sequence number and has 'acquire' memory
        /// semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \return
        /// The last-published sequence number.
        /// This sequence number is guaranteed to be equal to or after
        /// the specified \p sequence.
        ///
        /// \throws std::exception
        /// May throw any exception thrown by the WaitStrategy::wait_until_published()
        /// function.
        sequence_t wait_until_published(sequence_t sequence) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences);
        }

        /// \brief
        /// Block the calling thread until either the specified sequence number is
        /// published or a timeout has been exceeded.
        ///
        /// This operation synchronises with the corresponding call to publish()
        /// that publishes the returned sequence number and has 'acquire' memory
        /// semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeout
        /// The maximum amount of time to wait for the sequence number to
        /// be published.
        ///
        /// \return
        /// The last-published sequence number.
        /// If <tt>difference(result, sequence) >= 0</tt> then the operation has
        /// completed successfully and the requested sequence number has been
        /// published. Otherwise if <tt>difference(result, sequence) < 0</tt> then
        /// the operation has timed out.
        ///
        /// \throws std::exception
        /// May throw any exception thrown by the WaitStrategy::wait_until_published()
        /// function.
        template<class Rep, class Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences, timeout);
        }

        /// \brief
        /// Block the calling thread until either the specified sequence number is
        /// published or a timeout time has been exceeded.
        ///
        /// This operation synchronises with the corresponding call to publish()
        /// that publishes the returned sequence number and has 'acquire' memory
        /// semantics.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeoutTime
        /// Wait only until this point in time.
        ///
        /// \return
        /// The last-published sequence number.
        /// If <tt>difference(result, sequence) >= 0</tt> then the operation has
        /// completed successfully and the requested sequence number has been
        /// published. Otherwise if <tt>difference(result, sequence) < 0</tt> then
        /// the operation has timed out.
        ///
        /// \throws std::exception
        /// May throw any exception thrown by the WaitStrategy::wait_until_published()
        /// function.
        template<class Clock, class Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences, timeoutTime);
        }
        
        /// \brief
        /// Publish the specified sequence number.
        ///
        /// This indicates that all sequence numbers up to and including this
        /// sequence number are available for use by down-stream operations.
        ///
        /// This operation synchronises with calls to any of the wait_until_published()
        /// overloads that wait on the specified \p sequence. This operation has
        /// 'release' memory semantics.
        ///
        /// \param sequence
        /// The sequence number to publish.
        ///
        /// \throws std::exception
        /// May throw any exception thrown by the WaitStrategy::signal_all_when_blocking()
        /// method.
        void publish(sequence_t sequence)
        {
            m_lastPublished.store(sequence, std::memory_order_release);
            m_waitStrategy.signal_all_when_blocking();
        }
        
    private:
    
        friend class sequence_barrier_group<WaitStrategy>;
    
        WaitStrategy& m_waitStrategy;
    
        // Pad before/after to prevent false-sharing
        uint8_t m_pad1[CacheLineSize - sizeof(sequence_t)];
        std::atomic<sequence_t> m_lastPublished;
        uint8_t m_pad2[CacheLineSize - sizeof(sequence_t)];
        
    };
}

#endif
