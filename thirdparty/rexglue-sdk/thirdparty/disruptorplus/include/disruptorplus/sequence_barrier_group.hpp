#ifndef DISRUPTORPLUS_SEQUENCE_BARRIER_GROUP_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_BARRIER_GROUP_HPP_INCLUDED

#include "sequence.hpp"
#include "sequence_barrier.hpp"

#include <cassert>
#include <chrono>
#include <vector>

namespace disruptorplus
{
    /// \brief
    /// A sequence barrier group holds a collection of sequence barriers
    /// that can be used to wait until all of the sequence barriers have
    /// published a given sequence number.
    ///
    /// This can be used when one consumer in the processing pipeline must
    /// wait until multiple prior consumers in the processing pipeline have
    /// completed processing an item before starting to process that item.
    ///
    /// \tparam WaitStrategy
    /// A class that defines the strategy to use for blocking threads while
    /// waiting for a given sequence number to be published.
    /// Must implement the wait_strategy model.
    template<typename WaitStrategy>
    class sequence_barrier_group
    {
    public:
    
        /// \brief
        /// Initialise the sequence barrier group to the empty set of sequence barriers.
        ///
        /// \note
        /// You must add some \ref sequence_barrier items to this
        /// sequence barrier group before you can wait on it.
        ///
        /// \param waitStrategy
        /// The wait strategy to use for threads waiting on this sequence barrier group.
        /// The constructed sequence barrier will hold a reference to this object
        /// so callers must ensure the lifetime of the wait strategy exceeds that
        /// of the sequence barrier group.
        sequence_barrier_group(WaitStrategy& waitStrategy)
        : m_waitStrategy(waitStrategy)
        {}
        
        /// \brief
        /// Add a sequence barrier to the group.
        ///
        /// This operation is not thread-safe and must be called prior
        /// to sharing this object for use on multiple threads.
        ///
        /// The sequence barrier group holds onto a reference to the sequence
        /// barrier, so the passed barrier's lifetime must exceed that of the
        /// sequence barrier.
        ///
        /// \param barrier
        /// The sequence barrier to add to the group.
        /// This barrier must have been constructed with the same wait strategy
        /// object that this sequence barrier group was constructed with.
        ///
        /// \throw std::bad_alloc
        /// If there was insufficient memory to add the sequence barriers to
        /// the group.
        void add(const sequence_barrier<WaitStrategy>& barrier)
        {
            assert(&barrier.m_waitStrategy == &m_waitStrategy);
            m_sequences.push_back(&barrier.m_lastPublished);
        }
        
        /// \brief
        /// Add all sequence barriers in another group to this group.
        ///
        /// This operation is not thread-safe and must be called prior
        /// to sharing this object for use on multiple threads.
        ///
        /// The sequence barrier group holds onto a reference to all
        /// sequence barriers added to it, so the passed sequence barriers'
        /// lifetimes must exceed that of the sequence barrier.
        ///
        /// \param barrierGroup
        /// The sequence barrier group to add to the group.
        /// All sequence barriers added to the specified group at the time
        /// this call is made are added to this group.
        ///
        /// \throw std::bad_alloc
        /// If there was insufficient memory to add the sequence barriers to
        /// the group.
        void add(const sequence_barrier_group<WaitStrategy>& barrierGroup)
        {
            m_sequences.insert(
                m_sequences.end(),
                barrierGroup.m_sequences.begin(),
                barrierGroup.m_sequences.end());
        }
        
        /// \brief
        /// Query the sequence number of the least-advanced sequence barrier
        /// in the group.
        sequence_t last_published() const
        {
            assert(!m_sequences.empty());
            return minimum_sequence(m_sequences.size(), m_sequences.data());
        }
        
        /// \brief
        /// Block the calling thread until all sequence barriers in the group
        /// have advanced to at least the specified sequence.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \return
        /// The sequence number of the least-advanced sequence in the group.
        /// This is guaranteed to satisfy <tt>difference(result, sequence) >= 0</tt>.
        ///
        /// \throw std::exception
        /// Can throw any exception thrown by the associated
        /// \c WaitStrategy::wait_until_published() method.
        sequence_t wait_until_published(sequence_t sequence) const
        {
            assert(!m_sequences.empty());
            
            size_t count = m_sequences.size();
            
            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data());
        }

        /// \brief
        /// Block the calling thread until either all sequence barriers in
        /// the group had advanced to at least the specified sequence or
        /// a timeout has elapsed.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeout
        /// The maximum time to block the thread for.
        /// Note the thread may be blocked for slightly more or less than
        /// this time depending on operating system thread scheduling.
        ///
        /// \return
        /// If the requested \p sequence number was published by all barriers
        /// then returns the sequence number of the least-advanced of the
        /// sequence barriers in the group. Otherwise, if the operation timed
        /// out then returns some sequence number prior to \p sequence.
        ///
        /// \throw std::exception
        /// Can throw any exception thrown by the associated
        /// \c WaitStrategy::wait_until_published() method.
        template<class Rep, class Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            assert(!m_sequences.empty());
            
            size_t count = m_sequences.size();

            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data(), timeout);
        }
        
        /// \brief
        /// Block the calling thread until either all sequence barriers in
        /// the group had advanced to at least the specified sequence or
        /// a timeout time has passed.
        ///
        /// \param sequence
        /// The sequence number to wait for.
        ///
        /// \param timeoutTime
        /// The time after which this operation times out.
        /// Note the thread may be blocked for slightly more or less than
        /// this time depending on operating system thread scheduling.
        ///
        /// \return
        /// If the requested \p sequence number was published by all barriers
        /// then returns the sequence number of the least-advanced of the
        /// sequence barriers in the group. Otherwise, if the operation timed
        /// out then returns some sequence number prior to \p sequence.
        ///
        /// \throw std::exception
        /// Can throw any exception thrown by the associated
        /// \c WaitStrategy::wait_until_published() method.
        template<class Clock, class Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            assert(!m_sequences.empty());
            
            size_t count = m_sequences.size();

            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data(), timeoutTime);
        }
        
    private:
    
        WaitStrategy& m_waitStrategy;
        std::vector<const std::atomic<sequence_t>*> m_sequences;
    
    };
}

#endif
