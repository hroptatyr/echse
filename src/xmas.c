/*** xmas.c -- christmas stream
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

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */


/* christmas stream */
struct xmas_clo_s {
	enum {
		BEFORE_XMAS,
		ON_XMAS,
		BEFORE_BOXD,
		ON_BOXD,
	} state;
	unsigned int y;
};

static echs_event_t
__xmas(void *clo)
{
	DEFSTATE(XMAS);
	DEFSTATE(BOXD);
	struct xmas_clo_s *xc = clo;
	echs_event_t e;
#define state		(xc->state)
#define y		(xc->y)

	switch (state) {
	case BEFORE_XMAS:
		e.when = (echs_instant_t){y, 12U, 25U};
		e.what = ON(XMAS);
		state = ON_XMAS;
		break;
	case ON_XMAS:
		e.when = (echs_instant_t){y, 12U, 26U};
		e.what = OFF(XMAS);
		state = BEFORE_BOXD;
		break;
	case BEFORE_BOXD:
		e.when = (echs_instant_t){y, 12U, 26U};
		e.what = ON(BOXD);
		state = ON_BOXD;
		break;
	case ON_BOXD:
		e.when = (echs_instant_t){y, 12U, 27U};
		e.what = OFF(BOXD);
		state = BEFORE_XMAS;
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
	struct xmas_clo_s *xc = clo;

	switch (ctl) {
	case ECHS_STRCTL_CLONE: {
		struct xmas_clo_s *clone = malloc(sizeof(*xc));

		*clone = *xc;
		return clone;
	}
	case ECHS_STRCTL_UNCLONE:
		free(xc);
		break;
	default:
		break;
	}
	return NULL;
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	struct xmas_clo_s *clo;
#define state		(clo->state)
	unsigned int y;

	clo = malloc(sizeof(*clo));

	if (i.m < 12U || i.d <= 25U) {
		y = i.y;
		state = BEFORE_XMAS;
	} else if (i.d <= 26U) {
		y = i.y;
		state = ON_XMAS;
	} else if (i.d <= 27U) {
		y = i.y;
		state = ON_BOXD;
	} else {
		y = i.y + 1;
		state = BEFORE_XMAS;
	}
	clo->y = y;
#undef state
	return (echs_stream_t){__xmas, clo, __ctl};
}

void
free_echs_stream(echs_stream_t xmas_strm)
{
	struct xmas_clo_s *clo = xmas_strm.clo;

	if (clo != NULL) {
		free(clo);
	}
	return;
}

/* xmas.c ends here */
