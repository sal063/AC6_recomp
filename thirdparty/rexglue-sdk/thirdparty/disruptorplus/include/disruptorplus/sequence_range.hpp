#ifndef DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED

#include "sequence.hpp"

namespace disruptorplus
{
    /// \brief
    /// A sequence range represents a contiguous range of sequence numbers.
    ///
    /// A range has a \ref first() sequence number and a \ref size() which
    /// indicates the number of sequence numbers in the range.
    ///
    /// The range of sequence numbers may overflow the underlying integer
    /// storage type in which case the values wrap back around to zero.
    class sequence_range
    {
    public:
    
        /// \brief
        /// Construct to the empty range.
        sequence_range()
        : m_first(0)
        , m_size(0)
        {}
        
        /// \brief
        /// Construct to a range of \p size sequence numbers starting at \p first.
        ///
        /// \param first
        /// The first sequence number in the range.
        ///
        /// \param size
        /// The number of sequence numbers in the range.
        sequence_range(sequence_t first, size_t size)
        : m_first(first)
        , m_size(size)
        {}

        /// \brief
        /// Number of sequence numbers in this range.
        size_t size() const { return m_size; }
        
        /// \brief
        /// The first sequence number in the range.
        sequence_t first() const { return m_first; }
        
        /// \brief
        /// The last sequence number in the range.
        sequence_t last() const { return end() - 1; }
        
        /// \brief
        /// One-past the last sequence number in the range.
        sequence_t end() const { return static_cast<sequence_t>(m_first + m_size); }
        
        /// \brief
        /// Lookup the nth sequence number in the range.
        ///
        /// \param index
        /// The index of the nth sequence number in the range.
        /// Must be in range <tt>[0, size())</tt>.
        ///
        /// \return
        /// The nth sequence number in the range.
        /// Equivalent to <tt>this->first() + index</tt>.
        sequence_t operator[](size_t index) const { return static_cast<sequence_t>(m_first + index); }
        
    private:
    
        sequence_t m_first;
        size_t m_size;
        
    };
}

#endif
