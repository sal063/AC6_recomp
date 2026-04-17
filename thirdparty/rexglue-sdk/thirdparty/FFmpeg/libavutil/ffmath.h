/*
 * copyright (c) 2016 Ganesh Ajjanagadde <gajjanag@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * internal math functions header
 */

#ifndef AVUTIL_FFMATH_H
#define AVUTIL_FFMATH_H

#include "attributes.h"
#include "libm.h"
static av_always_inline double fastexp2(double xmm0) {
	//should upper bound be clamped too?
	xmm0 = xmm0 > -1022 ? xmm0 : -1022;
	
	double xmm2 = 4.8425778448581696;
	double xmm3 = 27.728333711624146;
	
	double xmm1 = floor(xmm0);
	xmm0 = xmm0 - xmm1;
	xmm1 = xmm1 + 1017.2740579843521;
	xmm2 = xmm2 - xmm0;
	
	//fmadd would be nice here
	xmm1 += (xmm0*-0.49013227410614491);
	
	xmm2 = xmm3 / xmm2;//this could be done via rcp_ss, the value will be within float32 range and we can refine
	xmm0 = xmm1 + xmm2;
	xmm0 = xmm0 * 4503599627370496.0;
	signed long long r0 = (signed long long)(xmm0);
	return *(double*)&r0;
}

/**
 * Compute 10^x for floating point values. Note: this function is by no means
 * "correctly rounded", and is meant as a fast, reasonably accurate approximation.
 * For instance, maximum relative error for the double precision variant is
 * ~ 1e-13 for very small and very large values.
 * This is ~2x faster than GNU libm's approach, which is still off by 2ulp on
 * some inputs.
 * @param x exponent
 * @return 10^x
 */
static av_always_inline double ff_exp10(double x)
{
    return fastexp2(M_LOG2_10 * x);
}

static av_always_inline float ff_exp10f(float x)
{
    return exp2f(M_LOG2_10 * x);
}

/**
 * Compute x^y for floating point x, y. Note: this function is faster than the
 * libm variant due to mainly 2 reasons:
 * 1. It does not handle any edge cases. In particular, this is only guaranteed
 * to work correctly for x > 0.
 * 2. It is not as accurate as a standard nearly "correctly rounded" libm variant.
 * @param x base
 * @param y exponent
 * @return x^y
 */
static av_always_inline float ff_fast_powf(float x, float y)
{
    return expf(logf(x) * y);
}

#endif /* AVUTIL_FFMATH_H */
