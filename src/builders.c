/*** builders.c -- stream building blocks
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <string.h>
#include "echse.h"
#include "instant.h"
#include "builders.h"

static echs_wday_t
__get_wday(echs_instant_t i)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int year = i.y;
	unsigned int res;

	year -= i.m < 3;
	res = year + year / 4U - year / 100U + year / 400U;
	res += t[i.m - 1U] + i.d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}


struct wday_clo_s {
	char *state;
	echs_wday_t wd;
	int in_lieu;
	echs_stream_t s;
};

static echs_event_t
__wday_after(void *clo)
{
	struct wday_clo_s *wdclo = clo;
	echs_event_t e = echs_stream_next(wdclo->s);
	echs_wday_t ref_wd = wdclo->wd;
	echs_wday_t when_wd = __get_wday(e.when);
	int add;

	/* now the magic bit, we want the number of days to add so that
	 * wd(e.when + X) == WD above, this is a simple modulo subtraction */
	add = ((unsigned int)ref_wd + 7U - (unsigned int)when_wd) % 7;
	/* however if the difference is naught add 7 days so we truly get
	 * the same weekday next week (after as in strictly-after) */
	e.when.d += add ?: wdclo->in_lieu;
	e.when = echs_instant_fixup(e.when);
	return e;
}

DEFUN echs_stream_t
echs_wday_after(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->in_lieu = 7;
	clo->s = s;
	return (echs_stream_t){__wday_after, clo};
}

DEFUN echs_stream_t
echs_wday_after_or_on(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->s = s;
	return (echs_stream_t){__wday_after, clo};
}

static echs_event_t
__wday_before(void *clo)
{
	struct wday_clo_s *wdclo = clo;
	echs_event_t e = echs_stream_next(wdclo->s);
	echs_wday_t ref_wd = wdclo->wd;
	echs_wday_t when_wd = __get_wday(e.when);
	int add;

	/* now the magic bit, we want the number of days to add so that
	 * wd(e.when + X) == WD above, this is a simple modulo subtraction */
	add = ((unsigned int)when_wd + 7U - (unsigned int)ref_wd) % 7;
	/* however if the difference is naught add 7 days so we truly get
	 * the same weekday next week (after as in strictly-after) */
	e.when.d -= add ?: wdclo->in_lieu;
	e.when = echs_instant_fixup(e.when);
	return e;
}

DEFUN echs_stream_t
echs_wday_before(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->in_lieu = 7;
	clo->s = s;
	return (echs_stream_t){__wday_before, clo};
}

DEFUN echs_stream_t
echs_wday_before_or_on(echs_stream_t s, echs_wday_t wd)
{
	struct wday_clo_s *clo = calloc(1, sizeof(*clo));

	clo->wd = wd;
	clo->s = s;
	return (echs_stream_t){__wday_before, clo};
}

DEFUN echs_stream_t
echs_free_wday(echs_stream_t s)
{
	struct wday_clo_s *clo = s.clo;
	echs_stream_t res = clo->s;

	if (clo->state != NULL) {
		/* prob strdup'd */
		free(clo->state);
		clo->state = NULL;
	}
	free(clo);
	return res;
}


struct every_clo_s {
	char *state;
	echs_instant_t next;
};

static echs_event_t
__every_year(void *clo)
{
	struct every_clo_s *eclo = clo;
	echs_instant_t next = eclo->next;

	eclo->next.y++;
	return (echs_event_t){echs_instant_fixup(next), eclo->state};
}

DEFUN echs_stream_t
echs_every_year(echs_instant_t i, echs_mon_t mon, unsigned int dom)
{
/* short for *-MON-DOM */
	struct every_clo_s *clo = calloc(1, sizeof(*clo));

	if (i.m < mon || i.m == mon && i.d <= dom) {
		i = (echs_instant_t){i.y, mon, dom, ECHS_ALL_DAY};
	} else {
		i = (echs_instant_t){i.y + 1, mon, dom, ECHS_ALL_DAY};
	}
	clo->next = i;
	return (echs_stream_t){__every_year, clo};
}

static echs_event_t
__every_month(void *clo)
{
	struct every_clo_s *eclo = clo;
	echs_instant_t next = eclo->next;

	eclo->next.m++;
	return (echs_event_t){echs_instant_fixup(next), eclo->state};
}

DEFUN echs_stream_t
echs_every_month(echs_instant_t i, unsigned int dom)
{
/* short for *-*-DOM */
	struct every_clo_s *clo = calloc(1, sizeof(*clo));

	if (i.d <= dom) {
		i = (echs_instant_t){i.y, i.m, dom, ECHS_ALL_DAY};
	} else {
		i = (echs_instant_t){i.y, i.m + 1, dom, ECHS_ALL_DAY};
	}
	clo->next = i;
	return (echs_stream_t){__every_month, clo};
}

DEFUN void
echs_free_every(echs_stream_t s)
{
	struct every_clo_s *clo = s.clo;

	if (clo->state != NULL) {
		/* prob strdup'd */
		free(clo->state);
		clo->state = NULL;
	}
	free(clo);
	return;
}


struct any_clo_s {
	char *state;
};

static void
__any_set_state(echs_stream_t s, const char *state)
{
	struct any_clo_s *any = s.clo;
	size_t z = strlen(state);

	/* most builders are atomic so use the BANG */
	any->state = malloc(1U + z + 1U/*for \nul*/);
	any->state[0] = '!';
	memcpy(any->state + 1U, state, z + 1U);
	return;
}

DEFUN void
echs_every_set_state(echs_stream_t s, const char *state)
{
	return __any_set_state(s, state);
}

DEFUN void
echs_wday_set_state(echs_stream_t s, const char *state)
{
	return __any_set_state(s, state);
}

/* builders.c ends here */
