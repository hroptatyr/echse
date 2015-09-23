/*** event.h -- some echs_event_t functionality
 *
 * Copyright (C) 2013-2014 Sebastian Freundt
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
#if !defined INCLUDED_event_h_
#define INCLUDED_event_h_

#include <stddef.h>
#include <stdbool.h>
#include "instant.h"
#include "range.h"
#include "state.h"
#include "oid.h"

typedef struct echs_event_s echs_event_t;

struct echs_event_s {
	echs_instant_t from;
	echs_instant_t till;
	echs_oid_t oid;
	echs_stset_t sts;
};


/* externals */
/**
 * Sort an array EV of NEV elements stable and in-place. */
extern void echs_event_sort(echs_event_t *restrict ev, size_t nev);


/* convenience */
static inline __attribute__((const, pure)) bool
echs_event_0_p(echs_event_t e)
{
	return echs_instant_0_p(e.from);
}

static inline __attribute__((const, pure)) bool
echs_event_lt_p(echs_event_t e1, echs_event_t e2)
{
	return echs_instant_lt_p(e1.from, e2.from);
}

static inline __attribute__((const, pure)) bool
echs_event_eq_p(echs_event_t e1, echs_event_t e2)
{
	return echs_oid_eq_p(e1.oid, e2.oid) &&
		echs_instant_eq_p(e1.from, e2.from);
}

static inline __attribute__((const, pure)) bool
echs_event_le_p(echs_event_t e1, echs_event_t e2)
{
	return echs_instant_lt_p(e1.from, e2.from) || echs_event_eq_p(e1, e2);
}

static inline __attribute__((const, pure)) echs_event_t
echs_nul_event(void)
{
	static const echs_event_t nul = {
		.from = {.u = 0UL},
		.till = {.u = 0UL}
	};
	return nul;
}

static inline __attribute__((const, pure)) bool
echs_nul_event_p(echs_event_t e)
{
	return echs_nul_instant_p(e.from);
}

static inline __attribute__((const, pure)) echs_range_t
echs_event_range(echs_event_t e)
{
	return (echs_range_t){e.from, e.till};
}

#endif	/* INCLUDED_event_h_ */
