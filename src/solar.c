/*** solar.c -- sun rise, sun set stream
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
/* These computations follow: http://www.stjarnhimlen.se/comp/riset.html */
#include <math.h>
#include <string.h>
#include "echse.h"
#include "instant.h"
#include "celest.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

struct clo_s {
	/* position on earth we're talking */
	struct cel_pos_s pos;

	/* our metronome, in cel's time scale */
	cel_d_t now;
	struct cel_rts_s rts;

	enum {
		ST_UNK,
		ST_RISE,
		ST_NOON,
		ST_SET,
	} state;
};


static echs_event_t
__sun(void *vclo)
{
#define YIELD(x)	return x
	DEFISTATE(SUNRISE);
	DEFISTATE(SUNSET);
	DEFISTATE(NOON);
	static const struct cel_calcopt_s opt = {
		.max_iter = 3UL,
	};
	struct clo_s *clo = vclo;
	echs_event_t e;

	while (1) {
		switch (clo->state) {
		default:
		case ST_UNK:
			/* inc now and get ourselves a new rts */
			clo->rts = cel_rts(sun, ++clo->now, clo->pos, opt);
			/* yield rise value next */
			clo->state = ST_RISE;

		case ST_RISE:
			/* after rise comes noon */
			clo->state = ST_NOON;
			if (LIKELY(cel_h_valid_p(clo->rts.rise))) {
				e.when = dh_to_instant(clo->now, clo->rts.rise);
				e.what = ITS(SUNRISE);
				YIELD(e);
			}

		case ST_NOON:
			/* after rise comes set */
			clo->state = ST_SET;
			e.when = dh_to_instant(clo->now, clo->rts.transit);
			e.what = ITS(NOON);
			YIELD(e);

		case ST_SET:
			/* after rise comes recalc */
			clo->state = ST_UNK;
			if (LIKELY(cel_h_valid_p(clo->rts.set))) {
				e.when = dh_to_instant(clo->now, clo->rts.set);
				e.what = ITS(SUNSET);
				YIELD(e);
			}
		}
	}
	/* cannot be reached */
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	struct clo_s *clo = calloc(1, sizeof(*clo));
	clo->now = instant_to_d(i) - 1;
	return (echs_stream_t){__sun, clo};
}

void
free_echs_stream(echs_stream_t s)
{
	struct clo_s *clo = s.clo;

	free(clo);
	return;
}

void
echs_stream_pset(echs_stream_t s, const char *key, struct echs_pset_s v)
{
	struct clo_s *clo = s.clo;

	switch (v.typ) {
	case ECHS_PSET_DBL:
		if (!strcmp(key, "long") || !strcmp(key, "longitude")) {
			clo->pos.lng = RAD(v.dval);
		} else if (!strcmp(key, "lat") || !strcmp(key, "latitude")) {
			clo->pos.lat = RAD(v.dval);
		} else if (!strcmp(key, "alt") || !strcmp(key, "altitude")) {
			clo->pos.alt = v.dval;
		}
		break;
	default:
		break;
	}
}

/* solar.c ends here */
