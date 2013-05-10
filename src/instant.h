/*** instant.h -- some echs_instant_t functionality
 *
 * Copyright (C) 2013 Sebastian Freundt
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
 ***/
#if !defined INCLUDED_instant_h_
#define INCLUDED_instant_h_

#include <stdbool.h>
#include "echse.h"

typedef struct echs_idiff_s echs_idiff_t;

struct echs_idiff_s {
	unsigned int dd;
	unsigned int msd;
};

/**
 * Fix up instants like the 32 Dec to become 01 Jan of the following year. */
extern echs_instant_t echs_instant_fixup(echs_instant_t);

extern echs_idiff_t echs_instant_diff(echs_instant_t end, echs_instant_t beg);

extern echs_instant_t echs_instant_add(echs_instant_t bas, echs_idiff_t add);


static inline __attribute__((pure)) bool
__inst_0_p(echs_instant_t x)
{
	return x.u == 0U;
}

static inline __attribute__((pure)) bool
__inst_lt_p(echs_instant_t x, echs_instant_t y)
{
	return (x.y < y.y || x.y == y.y &&
		(x.m < y.m || x.m == y.m &&
		 (x.d < y.d || x.d == y.d &&
		  (x.H < y.H || x.H == y.H &&
		   (x.M < y.M || x.M == y.M &&
		    (x.S < y.S || x.S == y.S &&
		     (x.ms < y.ms)))))));
}

static inline __attribute__((pure)) bool
__inst_le_p(echs_instant_t x, echs_instant_t y)
{
	return !(x.y > y.y || x.y == y.y &&
		 (x.m > y.m || x.m == y.m &&
		  (x.d > y.d || x.d == y.d &&
		   (x.H > y.H || x.H == y.H &&
		    (x.M > y.M || x.M == y.M &&
		     (x.S > y.S || x.S == y.S &&
		      (x.ms > y.ms)))))));
}

static inline __attribute__((pure)) bool
__inst_eq_p(echs_instant_t x, echs_instant_t y)
{
	return x.y == y.y && x.m == y.m && x.d == y.d &&
		x.H == y.H && x.M == y.M && x.S == y.S &&
		x.ms == y.ms;
}


/* convenience */
static inline bool
__event_0_p(echs_event_t e)
{
	return __inst_0_p(e.when);
}

static inline bool
__event_lt_p(echs_event_t e, echs_instant_t i)
{
	return __inst_lt_p(e.when, i);
}

static inline bool
__event_le_p(echs_event_t e, echs_instant_t i)
{
	return __inst_le_p(e.when, i);
}

#endif	/* INCLUDED_instant_h_ */
