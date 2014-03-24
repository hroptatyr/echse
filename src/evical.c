/*** evical.c -- simple icalendar parser for echse
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "evical.h"
#include "intern.h"
#include "bufpool.h"
#include "dt-strpf.h"
#include "nifty.h"

typedef struct evarr_s *evarr_t;

struct evarr_s {
	size_t nev;
	echs_event_t ev[];
};


static echs_instant_t
snarf_value(const char *s)
{
	static const char vd[] = "VALUE=DATE:";
	static const char vdt[] = "VALUE=DATE-TIME:";
	echs_instant_t res = {.u = 0U};

	if (0) {
		;
	} else if (!strncmp(s, vd, sizeof(vd) - 1)) {
		/* date value */
		s += sizeof(vd) - 1;
	} else if (!strncmp(s, vdt, sizeof(vdt) - 1)) {
		/* date-time value */
		s += sizeof(vdt) - 1;
	} else {
		/* could be VERSION=1.0 syntax */
		;
	}
	res = dt_strp(s);
	return res;
}

static void
snarf_fld(echs_event_t ev[static 1U], const char *line, size_t llen)
{
	static const char dtsta[] = "DTSTART";
	static const char dtend[] = "DTEND";
	static const char summ[] = "SUMMARY";
	static const char desc[] = "DESCRIPTION";
	const char *lp = line;
	const char *ep = line + llen;
	enum {
		FLD_UNK,
		FLD_DTSTART,
		FLD_DTEND,
		FLD_SUMM,
		FLD_DESC,
	} fld;

	if (0) {
		;
	} else if (!strncmp(line, dtsta, sizeof(dtsta) - 1)) {
		/* got DTSTART */
		fld = FLD_DTSTART;
		lp += sizeof(dtsta);
	} else if (!strncmp(line, dtend, sizeof(dtend) - 1)) {
		/* got DTEND */
		fld = FLD_DTEND;
		lp += sizeof(dtend);
	} else if (!strncmp(line, summ, sizeof(summ) - 1)) {
		/* got SUMMARY */
		fld = FLD_SUMM;
		lp += sizeof(summ);
	} else if (!strncmp(line, desc, sizeof(desc) - 1)) {
		/* got DESCRIPTION */
		fld = FLD_DESC;
		lp += sizeof(desc);
	} else {
		return;
	}

	/* do more string inspection here */
	if (LIKELY(ep[-1] == '\n')) {
		ep--;
	}
	if (UNLIKELY(ep[-1] == '\r')) {
		ep--;
	}

	switch (fld) {
	default:
	case FLD_UNK:
		/* how did we get here? */
		return;
	case FLD_DTSTART:
		ev->from = snarf_value(lp);
		break;
	case FLD_DTEND:
		ev->till = snarf_value(lp);
		break;
	case FLD_SUMM: {
		obint_t c;

		c = intern(lp, ep - lp);
		ev->uid = c;
		break;
	}
	case FLD_DESC:
		ev->desc = bufpool(lp, ep - lp).str;
		break;
	}
	return;
}

static evarr_t
read_ical(const char *fn)
{
	FILE *fp;
	char *line = NULL;
	size_t llen = 0U;
	enum {
		ST_UNK,
		ST_VEVENT,
	} st = ST_UNK;
	echs_event_t ev;
	evarr_t a = NULL;
	size_t nev = 0UL;

	if ((fp = fopen(fn, "r")) == NULL) {
		return NULL;
	}

	for (ssize_t nrd; (nrd = getline(&line, &llen, fp)) > 0;) {
		switch (st) {
			static const char beg[] = "BEGIN:VEVENT";
			static const char end[] = "END:VEVENT";
		default:
		case ST_UNK:
			/* check if line is a new vevent */
			if (!strncmp(line, beg, sizeof(beg) - 1)) {
				/* yep, set state to vevent */
				st = ST_VEVENT;
				ev = (echs_event_t){0U, 0U};
			}
			break;
		case ST_VEVENT:
			if (!strncmp(line, end, sizeof(end) - 1)) {
				/* oh, ok, better stop the parsing then
				 * and reset the state machine */
				if (a == NULL || nev >= a->nev) {
					/* resize */
					const size_t nu = 2 * nev ?: 64U;
					size_t nz = nu * sizeof(*a->ev);

					a = realloc(a, nz + sizeof(a));
					a->nev = nu;
				}
				/* assign */
				a->ev[nev++] = ev;
				/* reset to unknown state */
				st = ST_UNK;
				break;
			}
			/* otherwise interpret the line */
			snarf_fld(&ev, line, nrd);
			break;
		}
	}
	/* clean up reader resources */
	free(line);
	fclose(fp);
	/* massage result array */
	a->nev = nev;
	return a;
}


/* our event class */
struct evical_s {
	echs_evstrm_class_t class;
	/* event array */
	evarr_t ea;
	/* our iterator state */
	size_t i;
};

static echs_event_t next_evical_vevent(echs_evstrm_t);
static void free_evical_vevent(echs_evstrm_t);
static echs_evstrm_t clone_evical_vevent(echs_evstrm_t);

static const struct echs_evstrm_class_s evical_cls = {
	.next = next_evical_vevent,
	.free = free_evical_vevent,
	.clone = clone_evical_vevent,
};

static const echs_event_t nul;

static echs_evstrm_t
make_evical_vevent(evarr_t a)
{
	struct evical_s *res = malloc(sizeof(*res));

	res->class = &evical_cls;
	res->ea = a;
	res->i = 0U;
	return (echs_evstrm_t)res;
}

static void
free_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;

	if (LIKELY(this->ea != NULL)) {
		free(this->ea);
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;
	evarr_t a;

	if (UNLIKELY(this->ea == NULL)) {
		return NULL;
	}
	with (size_t z = this->ea->nev * sizeof(*a->ev) + sizeof(a)) {
		a = malloc(z);
		memcpy(a, this->ea, z);
	}
	return make_evical_vevent(a);
}

static echs_event_t
next_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;

	if (UNLIKELY(this->i >= this->ea->nev)) {
		return nul;
	}
	return this->ea->ev[this->i++];
}


#define T	echs_event_t

static inline __attribute__((pure)) bool
compare(T e1, T e2)
{
	return echs_instant_lt_p(e1.from, e2.from);
}

#include "wikisort.c"

echs_evstrm_t
make_echs_evical(const char *fn)
{
	evarr_t a;

	if ((a = read_ical(fn)) == NULL) {
		return (echs_evstrm_t){0U};
	}
	/* got it, sort it then */
	WikiSort(a->ev, a->nev);
	return make_evical_vevent(a);
}

/* evical.c ends here */
