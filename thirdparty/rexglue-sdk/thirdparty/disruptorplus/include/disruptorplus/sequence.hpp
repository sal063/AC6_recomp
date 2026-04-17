#ifndef DISRUPTORPLUS_SEQUENCE_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_HPP_INCLUDED

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cassert>

/// \file
/// \brief
/// Defines typedefs and utility functions relating to sequence numbers.
///
/// Sequence numbers are a central concept in Disruptor++ as sequence numbers
/// are used to indicate the order that items were added to a ring buffer
/// and hence their location in the ring buffer.
///
/// Sequence numbers can overflow back to zero so all comparisons of sequence
/// numbers need to take this into account by calling the
/// \ref disruptorplus::difference(sequence_t,sequence_t) function.

/// \brief
/// The \ref disruptorplus namespace provides abstractions that implement the
/// Disruptor data structure for multi-threaded producer/consumer queues.
namespace disruptorplus
{
    /// \brief
    /// Integer type used to represent sequence number of an item
    /// added to a ring buffer.
    ///
    /// The first item added to a ring buffer always has a sequence number
    /// of zero, with the second item having a sequence number of 1, etc.
    /// Sequence numbers wrap around back to zero when they overflow.
    typedef uint64_t sequence_t;
    
    /// \typedef sequence_diff_t
    /// \brief
    /// Integer type used to represent the difference between two
    /// \ref sequence_t values.
    typedef int64_t sequence_diff_t;

    /// \brief    
    /// Calculate the difference between two sequence numbers.
    ///
    /// Use this function to determine the relative order of two sequence
    /// numbers rather than just using \p a < \p b to handle the case where
    /// sequence numbers overflow back to zero.
    ///
    /// \param a The first sequence number.
    /// \param b The second sequence number.
    /// \return
    /// The difference between \p a and \p b. ie. (a - b).
    /// This value will be <0 if \p a precedes \p b, 0 if \p a == \p b,
    /// >0 if \p b precedes \p a.
    inline sequence_diff_t difference(sequence_t a, sequence_t b)
    {
        return static_cast<sequence_diff_t>(a - b);
    }
    
    /// \brief
    /// Calculate the minimum sequence number of an array of sequences.
    ///
    /// Calculates the minimum sequence number of the array of sequences
    /// taking into account any overflowed sequence numbers by using the
    /// first sequence as the zero-point.
    ///
    /// This assumes no two active sequence values will be more than
    /// <tt>(1 << (sizeof(sequence_t) * 8 - 2)) - 1</tt> different from each other.
    ///
    /// This operation implies acquire memory semantics on each of the sequences.
    ///
    /// \param count
    /// The number of elements in \p sequences.
    /// Must be greater than zero.
    ///
    /// \param sequences
    /// An array of pointers to sequence_t values containing the sequence
    /// numbers to read.
    ///
    /// \return
    /// The minimum sequence number read from \p sequences.
    /// ie. the sequence number, \c s, such that <tt>difference(s, sequences[i]) >= 0</tt>
    /// for all \c i in \[0, count)\[.
    inline sequence_t minimum_sequence(
        size_t count,
        const std::atomic<sequence_t>* const sequences[])
    {
        assert(count > 0);
        sequence_t minimum = sequences[0]->load(std::memory_order_acquire);
        for (size_t i = 1; i < count; ++i)
        {
            sequence_t seq = sequences[i]->load(std::memory_order_acquire);
            if (difference(seq, minimum) < 0)
            {
                minimum = seq;
            }
        }
        return minimum;
    }
    
    /// \brief
    /// Calculate the minimum sequence number of an array of sequences,
    /// short-circuiting if any of them precede a specified sequence number.
    ///
    /// Calculates the minimum sequence number of the array of sequences
    /// taking into account any overflowed sequence numbers by using the
    /// first sequence as the zero-point.
    ///
    /// This assumes no two active sequence values will be more than
    /// <tt>(1 << (sizeof(sequence_t) * 8 - 2)) - 1</tt> different from each other.
    ///
    /// If the returned sequence number does not precede the specified
    /// sequence number then this operation implies acquire memory semantics
    /// on each of the sequences, otherwise the memory semantics are undefined
    /// and this operation should not be used for synchronisation.
    ///
    /// \param minimum The minimum desired sequence number.
    /// \param count The number of elements in \p sequences.
    /// \param sequences An array of \p count pointers to sequence_t values to read.
    /// \return
    /// If no sequence numbers in \p sequences precede the \p minimum sequence
    /// number then the return value is the minimum of the sequence numbers.
    /// If any sequence numbers precede \p minimum then the return value is
    /// one of the sequence numbers that precedes \p minimum.
    inline sequence_t minimum_sequence_after(
        sequence_t minimum,
        size_t count,
        const std::atomic<sequence_t>* const sequences[])
    {
        assert(count > 0);
        sequence_diff_t minDelta = difference(sequences[0]->load(std::memory_order_acquire), minimum);
        for (size_t i = 1; i < count && minDelta >= 0; ++i)
        {
            sequence_t seq = sequences[i]->load(std::memory_order_acquire);
            minDelta = std::min(minDelta, difference(seq, minimum));
        }
        return static_cast<sequence_t>(minDelta + minimum);
    }
}

#endif
