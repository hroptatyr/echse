/*** yd.h -- helpers for year-doy representation
 *
 * Copyright (C) 2010-2020 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of echse.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **/
#if !defined INCLUDED_yd_h_
#define INCLUDED_yd_h_

#include <stdint.h>

#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */

struct yd_s {
	unsigned int y;
	unsigned int d;
};

struct md_s {
	unsigned int m;
	unsigned int d;
};


static inline struct md_s
__yd_to_md(struct yd_s yd)
{
/* stolen from dateutils */
#define GET_REM(x)	(rem[x])
	static const uint8_t rem[] = {
		19U, 19U, 18U, 14U, 13U, 11U, 10U, 8U, 7U, 6U, 4U, 3U, 1U, 0U,
	};
	unsigned int m;
	unsigned int d;
	unsigned int beef;
	unsigned int cake;

	/* get 32-adic doys */
	m = (yd.d + 19U) / 32U;
	d = (yd.d + 19U) % 32U;
	beef = GET_REM(m);
	cake = GET_REM(m + 1);

	/* put leap years into cake */
	if (UNLIKELY((yd.y % 4) == 0U && cake < 16U)) {
		/* note how all leap-affected cakes are < 16 */
		beef += beef < 16U;
		cake++;
	}

	if (d <= cake) {
		d = yd.d - ((m - 1) * 32U - 19U + beef);
	} else {
		d = yd.d - (m++ * 32U - 19U + cake);
	}
	return (struct md_s){.m = m, .d = d};
#undef GET_REM
}

static inline struct yd_s
__md_to_yd(unsigned int y, struct md_s md)
{
	static uint16_t __mon_yday[] = {
		/* this is \sum ml */
		0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};
	unsigned int doy = __mon_yday[md.m - 1] + md.d +
		(UNLIKELY((y % 4U) == 0) && md.m >= 3U);

	return (struct yd_s){.y = y, .d = doy};
}

#endif	/* INCLUDED_yd_h_ */
