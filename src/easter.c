/*** easter.c -- gregorian easter stream
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
#include "echse.h"
#include "instant.h"

#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

/* christmas stream */
static echs_instant_t
__easter(unsigned int y)
{
	/* compute gregorian easter date first */
	unsigned int a = y % 19U;
	unsigned int b = y / 4U;
	unsigned int c = b / 25U + 1;
	unsigned int d = 3U * c / 4U;
	unsigned int e;

	e = 19U * a + -((8U * c + 5) / 25U) + d + 15U;
	e %= 30U;
	e += (29578U - a - 32U * e) / 1024U;
	e = e - ((y % 7U) + b - d + e + 2) % 7U;
	return (echs_instant_t){y, e <= 31 ? 3U : 4U, e <= 31U ? e : e - 31U};
}

static enum {
	BEFORE_GOODFRI,
	START_OVER = BEFORE_GOODFRI,
	ON_GOODFRI,
	BEFORE_EASTER,
	ON_EASTER,
	BEFORE_EASTERMON,
	ON_EASTERMON,
	BEFORE_ASCENSION,
	ON_ASCENSION,
	BEFORE_PENTECOST,
	ON_PENTECOST,
} state;
static echs_instant_t easter;

static echs_event_t
__stream(void *UNUSED(clo))
{
	DEFSTATE(GOODFRI);
	DEFSTATE(EASTER);
	DEFSTATE(EASTERMON);
	DEFSTATE(ASCENSION);
	DEFSTATE(PENTECOST);
	struct echs_event_s e;

	switch (state) {
		echs_instant_t tmp;
	case BEFORE_GOODFRI:
		/* easter is assumed to point to easter sun */
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d -= 2, tmp));
		e.what = ON(GOODFRI);
		state = ON_GOODFRI;
		break;
	case ON_GOODFRI:
		/* easter is assumed to point to easter sun */
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d -= 1, tmp));
		e.what = OFF(GOODFRI);
		state = BEFORE_EASTER;
		break;
	case BEFORE_EASTER:
		e.when = easter;
		e.what = ON(EASTER);
		state = ON_EASTER;
		break;
	case ON_EASTER:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 1, tmp));
		e.what = OFF(EASTER);
		state = BEFORE_EASTERMON;
		break;
	case BEFORE_EASTERMON:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 1, tmp));
		e.what = ON(EASTERMON);
		state = ON_EASTERMON;
		break;
	case ON_EASTERMON:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 2, tmp));
		e.what = OFF(EASTERMON);
		state = BEFORE_ASCENSION;
		break;
	case BEFORE_ASCENSION:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 40, tmp));
		e.what = ON(ASCENSION);
		state = ON_ASCENSION;
		break;
	case ON_ASCENSION:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 41, tmp));
		e.what = OFF(ASCENSION);
		state = BEFORE_PENTECOST;
		break;
	case BEFORE_PENTECOST:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 49, tmp));
		e.what = ON(PENTECOST);
		state = ON_PENTECOST;
		break;
	case ON_PENTECOST:
		tmp = easter;
		e.when = echs_instant_fixup((tmp.d += 50, tmp));
		e.what = OFF(PENTECOST);
		state = START_OVER;
		easter = __easter(easter.y + 1);
		break;
	default:
		abort();
	}
	return e;
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	echs_instant_t tmp;

	easter = __easter(i.y);
	if ((tmp = easter, tmp.d -= 2,
	     __inst_le_p(i, echs_instant_fixup(tmp)))) {
		state = BEFORE_GOODFRI;
	} else if (__inst_le_p(i, easter)) {
		state = BEFORE_EASTER;
	} else if ((tmp = easter, tmp.d++,
		    __inst_le_p(i, echs_instant_fixup(tmp)))) {
		state = BEFORE_EASTERMON;
	} else if ((tmp = easter, tmp.d += 40,
		    __inst_le_p(i, echs_instant_fixup(tmp)))) {
		state = BEFORE_ASCENSION;
	} else if ((tmp = easter, tmp.d += 49,
		    __inst_le_p(i, echs_instant_fixup(tmp)))) {
		state = BEFORE_PENTECOST;
	} else {
		easter = __easter(i.y + 1);
		state = START_OVER;
	}
	return (echs_stream_t){__stream, NULL};
}

/* easter.c ends here */
