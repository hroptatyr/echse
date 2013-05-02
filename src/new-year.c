/*** new-year.c -- new-year's stream
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
#include <stdlib.h>
#include "echse.h"
#include "strctl.h"

#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */


/* new-year stream */
struct newy_clo_s {
	enum {
		BEFORE_NEWYEAR,
		ON_NEWYEAR,
	} state;
	unsigned int y;
};

static echs_event_t
__stream(void *clo)
{
	DEFSTATE(NEWYEAR);
	struct newy_clo_s *nyc = clo;
	struct echs_event_s e;
#define state		(nyc->state)
#define y		(nyc->y)

	switch (state) {
	case BEFORE_NEWYEAR:
		e.when = (echs_instant_t){y, 1U, 1U};
		e.what = ON(NEWYEAR);
		state = ON_NEWYEAR;
		break;
	case ON_NEWYEAR:
		e.when = (echs_instant_t){y, 1U, 2U};
		e.what = OFF(NEWYEAR);
		state = BEFORE_NEWYEAR;
		y++;
		break;
	default:
		abort();
	}
#undef state
#undef y
	return e;
}

static void*
__ctl(echs_strctl_t ctl, void *clo, ...)
{
	struct newy_clo_s *nyc = clo;

	switch (ctl) {
	case ECHS_STRCTL_CLONE: {
		struct newy_clo_s *clone = malloc(sizeof(*nyc));

		*clone = *nyc;
		return clone;
	}
	case ECHS_STRCTL_UNCLONE:
		free(nyc);
		break;
	default:
		break;
	}
	return NULL;
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	struct newy_clo_s *clo;

	clo = malloc(sizeof(*clo));

	if (i.m > 1U || i.d > 2U) {
		clo->y = i.y + 1;
		clo->state = BEFORE_NEWYEAR;
	} else if (i.d < 2U) {
		clo->y = i.y;
		clo->state = BEFORE_NEWYEAR;
	} else {
		clo->y = i.y;
		clo->state = ON_NEWYEAR;
	}
	return (echs_stream_t){__stream, clo, __ctl};
}

void
free_echs_stream(echs_stream_t newy_strm)
{
	struct newy_clo_s *clo = newy_strm.clo;

	if (clo != NULL) {
		free(clo);
	}
	return;
}

/* new-year.c ends here */
