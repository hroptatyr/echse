/*** event.h -- some echs_event_t functionality
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
#if !defined INCLUDED_event_h_
#define INCLUDED_event_h_

#include "echse.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */


static inline __attribute__((pure, const)) char
echs_event_mdfr(echs_event_t e)
{
	if (UNLIKELY(e.what == NULL)) {
		return '!';
	}
	switch (*e.what) {
	case '!':
	case '~':
		return *e.what;
	default:
		break;
	}
	return '\0';
}

static inline echs_instant_t
echs_event_beg(echs_event_t e)
{
	switch (echs_event_mdfr(e)) {
	default:
	case '!':
		if (echs_instant_all_day_p(e.when)) {
			e.when.H = 0U;
		} else if (echs_instant_all_sec_p(e.when)) {
			e.when.ms = 0U;
		}
		break;
	case '~':
		return (echs_instant_t){0};
	}
	return e.when;
}

static inline echs_instant_t
echs_event_end(echs_event_t e)
{
	switch (echs_event_mdfr(e)) {
	default:
		return (echs_instant_t){0};
	case '!':
		if (echs_instant_all_day_p(e.when)) {
			e.when = (e.when.d++, echs_instant_fixup(e.when));
		} else if (echs_instant_all_sec_p(e.when)) {
			e.when = (e.when.S++, echs_instant_fixup(e.when));
		}
	case '~':
		if (echs_instant_all_day_p(e.when)) {
			e.when.H = 0U;
		} else if (echs_instant_all_sec_p(e.when)) {
			e.when.ms = 0U;
		}
		break;
	}
	return e.when;
}

#endif	/* INCLUDED_event_h_ */
