#ifndef DISRUPTORPLUS_CONFIG_HPP_INCLUDED
#define DISRUPTORPLUS_CONFIG_HPP_INCLUDED

#include <cstddef>

namespace disruptorplus
{
    /// \brief
    /// The expected size of a cache-line in bytes.
    ///
    /// Some data structures are padded according to this value to
    /// ensure certain data members do not share a cache line with
    /// other data in order to reduce the problem of false-sharing.
    ///
    /// The value of 64 bytes is typical for x86/x64 architectures.
    const size_t CacheLineSize = 64;
}

#endif
