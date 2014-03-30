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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "evical.h"
#include "intern.h"
#include "bufpool.h"
#include "bitint.h"
#include "dt-strpf.h"
#include "evrrul.h"
#include "nifty.h"
#include "evical-gp.c"
#include "evrrul-gp.c"

typedef struct evarr_s *evarr_t;

struct evarr_s {
	size_t nev;
	echs_event_t ev[];
};

struct rrulsp_s {
	echs_freq_t freq;
	unsigned int count;
	unsigned int inter;

	bitint31_t dom;
	bitint383_t doy;

	/* we'll store mon->1, tue->2, ..., 1mon->8, 2mon->15, ...
	 * -1mon->-1, -1tue->-2, ..., -2mon->-8, -3mon->-15, ... */
	bitint383_t dow;

	bituint31_t mon;
	bitint63_t wk;

	bituint31_t H;
	bituint63_t M, S;

	bitint383_t pos;
	bitint383_t easter;
};


static echs_instant_t
snarf_value(const char *s)
{
	echs_instant_t res = {.u = 0U};
	const char *sp;

	if (LIKELY((sp = strchr(s, ':')) != NULL)) {
		res = dt_strp(sp + 1);
	}
	return res;
}

static echs_freq_t
snarf_freq(const char *spec)
{
	switch (*spec) {
	case 'Y':
		return FREQ_YEARLY;
	case 'M':
		if (UNLIKELY(spec[1] == 'I')) {
			return FREQ_MINUTELY;
		}
		return FREQ_MONTHLY;
	case 'W':
		return FREQ_WEEKLY;
	case 'D':
		return FREQ_DAILY;
	case 'H':
		return FREQ_HOURLY;
	case 'S':
		return FREQ_SECONDLY;
	default:
		break;
	}
	return FREQ_NONE;
}

static echs_wday_t
snarf_wday(const char *s)
{
	switch (*s) {
	case 'M':
		return MON;
	case 'W':
		return WED;
	case 'F':
		return FRI;

	case 'R':
		return THU;
	case 'A':
		return SAT;

	case 'T':
		if (s[1U] == 'H') {
			return THU;
		}
		return TUE;
	case 'S':
		if (s[1U] == 'A') {
			return SAT;
		}
		return SUN;
	default:
		break;
	}
	return MIR;
}

static void
snarf_rrule(const char *s, size_t z)
{
	struct rrulsp_s rr = {FREQ_NONE};

	for (const char *sp = s, *const ep = s + z, *eofld;
	     sp < ep; sp = eofld + 1) {
		const struct rrul_key_cell_s *c;
		const char *kv;
		size_t kz;

		if (UNLIKELY((eofld = strchr(sp, ';')) == NULL)) {
			eofld = ep;
		}
		/* find the key-val separator (=) */
		if (UNLIKELY((kv = strchr(sp, '=')) == NULL)) {
			kz = eofld - sp;
		} else {
			kz = kv - sp;
		}
		/* try a lookup */
		if (UNLIKELY((c = __evrrul_key(sp, kz)) == NULL)) {
			/* not found */
			continue;
		}
		/* otherwise do a bit of inspection */
		switch (c->key) {
			long int tmp;
			char *on;
		case KEY_FREQ:
			/* read the spec as well */
			rr.freq = snarf_freq(++kv);
			break;

		case KEY_COUNT:
		case KEY_INTER:
			tmp = atol(++kv);
			switch (c->key) {
			case KEY_COUNT:
				rr.count = (unsigned int)tmp;
				break;
			case KEY_INTER:
				rr.inter = (unsigned int)tmp;
				break;
			}
			break;

		case BY_WDAY:
			/* this one's special in that weekday names
			 * are allowed to follow the indicator number */
			do {
				echs_wday_t w;

				tmp = strtol(++kv, &on, 10);
				if ((w = snarf_wday(on)) == MIR) {
					continue;
				}
				/* otherwise assign */
				ass_bi383(&rr.dow, (tmp *= 7, tmp += w, tmp));
			} while ((kv = strchr(on, ',')) != NULL);
			break;
		case BY_MON:
		case BY_HOUR:
		case BY_MIN:
		case BY_SEC:
			do {
				tmp = strtoul(++kv, &on, 10);
				switch (c->key) {
				case BY_MON:
					rr.mon = ass_bui31(rr.mon, tmp);
					break;
				case BY_HOUR:
					rr.H = ass_bui31(rr.H, tmp);
					break;
				case BY_MIN:
					rr.M = ass_bui63(rr.M, tmp);
					break;
				case BY_SEC:
					rr.S = ass_bui63(rr.S, tmp);
					break;
				}
			} while (*(kv = on) == ',');
			break;

		case BY_MDAY:
		case BY_WEEK:
		case BY_YDAY:
		case BY_POS:
		case BY_EASTER:
			/* these ones take +/- values */
			do {
				tmp = strtol(++kv, &on, 10);
				switch (c->key) {
				case BY_MDAY:
					rr.dom = ass_bi31(rr.dom, tmp - 1);
					break;
				case BY_WEEK:
					rr.wk = ass_bi63(rr.wk, tmp);
					break;
				case BY_YDAY:
					ass_bi383(&rr.doy, tmp);
					break;
				case BY_POS:
					ass_bi383(&rr.pos, tmp);
					break;
				case BY_EASTER:
					ass_bi383(&rr.easter, tmp);
					break;
				}
			} while (*(kv = on) == ',');
			break;

		default:
		case KEY_UNK:
			break;
		}
	}
	return;
}

static void
snarf_fld(echs_event_t ev[static 1U], const char *line, size_t llen)
{
	const char *lp;
	const char *ep = line + llen;
	const struct ical_fld_cell_s *c;

	if (UNLIKELY((lp = strpbrk(line, ":;")) == NULL)) {
		return;
	} else if (UNLIKELY((c = __evical_fld(line, lp - line)) == NULL)) {
		return;
	}
	/* do more string inspection here */
	if (LIKELY(ep[-1] == '\n')) {
		ep--;
	}
	if (UNLIKELY(ep[-1] == '\r')) {
		ep--;
	}

	switch (c->fld) {
	default:
	case FLD_UNK:
		/* how did we get here */
		return;
	case FLD_DTSTART:
		ev->from = snarf_value(lp);
		break;
	case FLD_DTEND:
		ev->till = snarf_value(lp);
		break;
	case FLD_RRULE:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		/* otherwise snarf him */
		snarf_rrule(lp, ep - lp);
		break;
	case FLD_SUMM:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		with (obint_t uid = intern(lp, ep - lp)) {
			ev->uid = uid;
		}
		break;
	case FLD_DESC:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
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

	/* little probe first
	 * luckily BEGIN:VCALENDAR\n is exactly 16 bytes */
	with (ssize_t nrd) {
		static const char hdr[] = "BEGIN:VCALENDAR";

		if (UNLIKELY((nrd = getline(&line, &llen, fp)) <= 0)) {
			/* must be bollocks then */
			goto clo;
		} else if ((size_t)nrd < sizeof(hdr)) {
			/* still looks like bollocks */
			goto clo;
		} else if (memcmp(line, hdr, sizeof(hdr) - 1)) {
			/* also bollocks */
			goto clo;
		}
		/* otherwise, this looks legit
		 * oh, but keep your fingers crossed anyway */
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
	/* massage result array */
	a->nev = nev;
	/* clean up reader resources */
	free(line);
clo:
	fclose(fp);
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
		return NULL;
	}
	/* got it, sort it then */
	WikiSort(a->ev, a->nev);
	return make_evical_vevent(a);
}


struct evrrul_s {
	echs_evstrm_class_t class;
	/* the actual recurrence rule */
	struct rrulsp_s rr;
};

static echs_event_t next_evrrul(echs_evstrm_t);
static void free_evrrul(echs_evstrm_t);
static echs_evstrm_t clone_evrrul(echs_evstrm_t);

static const struct echs_evstrm_class_s evrrul_cls = {
	.next = next_evrrul,
	.free = free_evrrul,
	.clone = clone_evrrul,
};

static echs_evstrm_t
make_evrrul(const struct rrulsp_s *rr)
{
	struct evrrul_s *res = malloc(sizeof(*res));

	res->class = &evrrul_cls;
	res->rr = *rr;
	return (echs_evstrm_t)res;
}

static void
free_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	free(this);
	return;
}

static echs_evstrm_t
clone_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	return make_evrrul(&this->rr);
}

static echs_event_t
next_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	return nul;
}


echs_evstrm_t
make_echs_evrrul(const char *fn)
{
	return NULL;
}

/* evical.c ends here */
