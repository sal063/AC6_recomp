#ifndef DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED
#define DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED

#include "sequence.hpp"

#include <memory>
#include <cassert>

namespace disruptorplus
{
    /// \brief
    /// A ring buffer is a buffer of size power-of-two that can
    /// be indexed using a sequence number.
    ///
    /// A given slot, \c i, in the ring buffer is addressed by any sequence
    /// number that has the form <tt>n * size() + i</tt> for some \c n.
    ///
    /// A ring buffer is typically used in conjunction with a claim-strategy
    /// for writers to claim a slot in the ring buffer, and one or more
    /// sequence-barriers for readers to indicate where in the ring buffer
    /// they have processed up to.
    ///
    /// \tparam T
    /// The type of elements the ring buffer.
    /// This type must be default-constructible.
    template<typename T>
    class ring_buffer
    {
    public:
    
        /// \brief
        /// The type of elements in the ring buffer.
        typedef T value_type;
        
        /// \brief
        /// A type that is a reference to a ring buffer element.
        typedef T& reference;
        
        /// \brief
        /// A type that is a const-reference to a ring buffer element.
        typedef const T& const_reference;
    
        /// \brief
        /// Constructs a ring buffer of a specified size.
        ///
        /// Allocates the buffer on the heap and default-constructs all
        /// elements of the buffer.
        ///
        /// \param size
        /// The desired size of the ring buffer.
        /// Must be a power of two, eg. 16384.
        ///
        /// \throws std::bad_alloc
        /// If there was insufficient memory to allocate the buffer.
        ring_buffer(size_t size)
        : m_size(size)
        , m_mask(size - 1)
        , m_data(new T[size])
        {
            // Check that size was a power-of-two.
            assert(m_size > 0 && (m_size & m_mask) == 0);
        }
        
        /// \brief
        /// The number of elements in the ring buffer.
        ///
        /// This will always be a power-of-two.
        size_t size() const
        {
            return m_size;
        }
        
        /// \brief
        /// Obtain a reference to the ring buffer element corresponding to the specified
        /// sequence number.
        ///
        /// \param seq
        /// The sequence number of the item in the ring buffer.
        /// The slot in the ring buffer is calculated as the sequence number
        /// modulo \ref size().
        reference operator[](sequence_t seq)
        {
            return m_data[static_cast<size_t>(seq) & m_mask];
        }
        
        /// \copydoc ring_buffer::operator[](sequence_t)
        const_reference operator[](sequence_t seq) const
        {
            return m_data[static_cast<size_t>(seq) & m_mask];
        }
        
    private:

        // Disable copy-construction
        ring_buffer(const ring_buffer&);
    
        const size_t m_size;
        const size_t m_mask;
        std::unique_ptr<T[]> m_data;
    
    };
}

#endif
