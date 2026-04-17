// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Copyright (c) 2020 Pavel Kirienko
// Authors: Pavel Kirienko <pavel.kirienko@zubax.com>

#include "internal.hpp"

TEST_CASE("Private: log2")
{
    using internal::log2Floor;
    // The function is only defined for x>=1.
    REQUIRE(log2Floor(1) == 0);
    REQUIRE(log2Floor(2) == 1);
    REQUIRE(log2Floor(3) == 1);
    REQUIRE(log2Floor(4) == 2);
    REQUIRE(log2Floor(30) == 4);
    REQUIRE(log2Floor(60) == 5);
    REQUIRE(log2Floor(64) == 6);
}

TEST_CASE("Private: pow2")
{
    using internal::pow2;
    REQUIRE(pow2(0) == 1);
    REQUIRE(pow2(1) == 2);
    REQUIRE(pow2(2) == 4);
    REQUIRE(pow2(3) == 8);
    REQUIRE(pow2(4) == 16);
    REQUIRE(pow2(5) == 32);
    REQUIRE(pow2(6) == 64);
    REQUIRE(pow2(7) == 128);
    REQUIRE(pow2(8) == 256);
    REQUIRE(pow2(9) == 512);
}

TEST_CASE("Private: roundUpToPowerOf2")
{
    using internal::pow2;
    using internal::roundUpToPowerOf2;
    // The function is only defined for x>=2.
    REQUIRE(roundUpToPowerOf2(2) == 2);
    REQUIRE(roundUpToPowerOf2(3) == 4);
    REQUIRE(roundUpToPowerOf2(4) == 4);
    REQUIRE(roundUpToPowerOf2(5) == 8);
    REQUIRE(roundUpToPowerOf2(6) == 8);
    REQUIRE(roundUpToPowerOf2(7) == 8);
    REQUIRE(roundUpToPowerOf2(8) == 8);
    REQUIRE(roundUpToPowerOf2(9) == 16);
    REQUIRE(roundUpToPowerOf2(10) == 16);
    REQUIRE(roundUpToPowerOf2(11) == 16);
    REQUIRE(roundUpToPowerOf2(12) == 16);
    REQUIRE(roundUpToPowerOf2(13) == 16);
    REQUIRE(roundUpToPowerOf2(14) == 16);
    REQUIRE(roundUpToPowerOf2(15) == 16);
    REQUIRE(roundUpToPowerOf2(16) == 16);
    REQUIRE(roundUpToPowerOf2(17) == 32);
    REQUIRE(roundUpToPowerOf2(32) == 32);
    REQUIRE(roundUpToPowerOf2(2147483647U) == 2147483648U);
    REQUIRE(roundUpToPowerOf2(2147483648U) == 2147483648U);
}
