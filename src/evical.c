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
#include <stddef.h>
#include <stdbool.h>
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

typedef struct vearr_s *vearr_t;
typedef uintptr_t goptr_t;

struct dtlst_s {
	size_t ndt;
	goptr_t dt;
};

struct rrlst_s {
	size_t nr;
	goptr_t r;
};

struct ical_vevent_s {
	echs_event_t ev;
	/* pointers into the global rrul/xrul array */
	struct rrlst_s rr;
	struct rrlst_s xr;
	/* points into the global xdat/rdat array */
	struct dtlst_s rd;
	struct dtlst_s xd;
};

struct vearr_s {
	size_t nev;
	struct ical_vevent_s ev[];
};


/* global rrule array */
static size_t zgrr;
static goptr_t ngrr;
static struct rrulsp_s *grr;

/* global exrule array */
static size_t zgxr;
static goptr_t ngxr;
static struct rrulsp_s *gxr;

/* global exdate array */
static size_t zgxd;
static goptr_t ngxd;
static echs_instant_t *gxd;

/* global rdate array */
static size_t zgrd;
static goptr_t ngrd;
static echs_instant_t *grd;

#define CHECK_RESIZE(id, iniz, nitems)					\
	if (UNLIKELY(!z##id)) {						\
		/* leave the first one out */				\
		id = malloc((z##id = iniz) * sizeof(*id));		\
		memset(id, 0, sizeof(*id));				\
	}								\
	if (UNLIKELY(n##id + nitems > z##id)) {				\
		do {							\
			id = realloc(id, (z##id *= 2U) * sizeof(*id));	\
		} while (n##id + nitems > z##id);			\
	}

static goptr_t
add_to_grr(struct rrulsp_s rr)
{
	goptr_t res;

	CHECK_RESIZE(grr, 16U, 1U);
	grr[res = ngrr++] = rr;
	return res;
}

static const struct rrulsp_s*
get_grr(goptr_t d)
{
	if (UNLIKELY(grr == NULL)) {
		return NULL;
	}
	return grr + d;
}

static goptr_t
add_to_gxr(struct rrulsp_s xr)
{
	goptr_t res;

	CHECK_RESIZE(gxr, 16U, 1U);
	gxr[res = ngxr++] = xr;
	return res;
}

static const struct rrulsp_s*
get_gxr(goptr_t d)
{
	if (UNLIKELY(gxr == NULL)) {
		return NULL;
	}
	return gxr + d;
}

static goptr_t
add_to_gxd(struct dtlst_s xdlst)
{
	const echs_instant_t *dp = (const echs_instant_t*)xdlst.dt;
	const size_t nd = xdlst.ndt;
	goptr_t res;

	CHECK_RESIZE(gxd, 64U, nd);
	memcpy(gxd + ngxd, dp, nd * sizeof(*dp));
	res = ngxd, ngxd += nd;
	return res;
}

static echs_instant_t
get_gxd(goptr_t d)
{
	if (UNLIKELY(gxd == NULL)) {
		return echs_nul_instant();
	}
	return gxd[d];
}

static goptr_t
add_to_grd(struct dtlst_s rdlst)
{
	const echs_instant_t *dp = (const echs_instant_t*)rdlst.dt;
	const size_t nd = rdlst.ndt;
	goptr_t res;

	CHECK_RESIZE(grd, 64U, nd);
	memcpy(grd + ngrd, dp, nd * sizeof(*dp));
	res = ngrd, ngrd += nd;
	return res;
}

static echs_instant_t
get_grd(goptr_t d)
{
	if (UNLIKELY(grd == NULL)) {
		return echs_nul_instant();
	}
	return grd[d];
}


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

static struct rrulsp_s
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

		case KEY_UNTIL:
			rr.until = dt_strp(++kv);
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
	return rr;
}

static struct dtlst_s
snarf_dtlst(const char *line, size_t llen)
{
	static size_t zdt;
	static echs_instant_t *dt;
	size_t ndt = 0UL;

	for (const char *sp = line, *const ep = line + llen, *eod;
	     sp < ep; sp = eod + 1U) {
		echs_instant_t in;

		if (UNLIKELY((eod = strchr(sp, ',')) == NULL)) {
			eod = ep;
		}
		if (UNLIKELY(echs_instant_0_p(in = dt_strp(sp)))) {
			continue;
		}
		CHECK_RESIZE(dt, 64U, 1U);
		dt[ndt++] = in;
	}
	return (struct dtlst_s){ndt, (goptr_t)dt};
}

static void
snarf_fld(struct ical_vevent_s ve[static 1U], const char *line, size_t llen)
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
	case FLD_DTEND:
		with (echs_instant_t i = snarf_value(lp)) {
			switch (c->fld) {
			case FLD_DTSTART:
				ve->ev.from = i;
				break;
			case FLD_DTEND:
				ve->ev.till = i;
				break;
			}
		}
		break;
	case FLD_XDATE:
	case FLD_RDATE:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		/* otherwise snarf */
		with (struct dtlst_s l = snarf_dtlst(lp, ep - lp)) {
			if (l.ndt == 0UL) {
				break;
			}
			switch (c->fld) {
				goptr_t go;
			case FLD_XDATE:
				go = add_to_gxd(l);
				if (!ve->xd.ndt) {
					ve->xd.dt = go;
				}
				ve->xd.ndt += l.ndt;
				break;
			case FLD_RDATE:
				go = add_to_grd(l);
				if (!ve->rd.ndt) {
					ve->rd.dt = go;
				}
				ve->xd.ndt += l.ndt;
				break;
			}
		}
		break;
	case FLD_RRULE:
	case FLD_XRULE:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		/* otherwise snarf him */
		for (struct rrulsp_s r;
		     (r = snarf_rrule(lp, ep - lp)).freq != FREQ_NONE;) {
			goptr_t x;

			switch (c->fld) {
			case FLD_RRULE:
				/* bang to global array */
				x = add_to_grr(r);

				if (!ve->rr.nr++) {
					ve->rr.r = x;
				}
				break;
			case FLD_XRULE:
				/* bang to global array */
				x = add_to_gxr(r);

				if (!ve->xr.nr++) {
					ve->xr.r = x;
				}
				break;
			}
			/* this isn't supposed to be a for-loop */
			break;
		}
		break;
	case FLD_SUMM:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		with (obint_t uid = intern(lp, ep - lp)) {
			ve->ev.uid = uid;
		}
		break;
	case FLD_DESC:
		if (UNLIKELY(*lp++ != ':' && (lp = strchr(lp, ':')) == NULL)) {
			break;
		}
		ve->ev.desc = bufpool(lp, ep - lp).str;
		break;
	}
	return;
}

static vearr_t
read_ical(const char *fn)
{
	FILE *fp;
	char *line = NULL;
	size_t llen = 0U;
	enum {
		ST_UNK,
		ST_VEVENT,
	} st = ST_UNK;
	struct ical_vevent_s ve;
	size_t nve = 0UL;
	vearr_t a = NULL;

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
				/* and rinse our bucket */
				memset(&ve, 0, sizeof(ve));
			}
			break;
		case ST_VEVENT:
			if (!strncmp(line, end, sizeof(end) - 1)) {
				/* oh, ok, better stop the parsing then
				 * and reset the state machine */
				if (a == NULL || nve >= a->nev) {
					/* resize */
					const size_t nu = 2 * nve ?: 64U;
					size_t nz = nu * sizeof(*a->ev);

					a = realloc(a, nz + sizeof(a));
					a->nev = nu;
				}
				/* assign */
				a->ev[nve++] = ve;
				/* reset to unknown state */
				st = ST_UNK;
				break;
			}
			/* otherwise interpret the line */
			snarf_fld(&ve, line, nrd);
			break;
		}
	}
	/* massage result array */
	a->nev = nve;
	/* clean up reader resources */
	free(line);
clo:
	fclose(fp);
	return a;
}


/* our event class */
struct evical_s {
	echs_evstrm_class_t class;
	/* our iterator state */
	size_t i;
	/* array size and data */
	size_t nev;
	echs_event_t ev[];
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
make_evical_vevent(const echs_event_t *ev, size_t nev)
{
	const size_t zev = nev * sizeof(*ev);
	struct evical_s *res = malloc(sizeof(*res) + zev);

	res->class = &evical_cls;
	res->i = 0U;
	res->nev = nev;
	memcpy(res->ev, ev, zev);
	return (echs_evstrm_t)res;
}

static void
free_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;

	free(this);
	return;
}

static echs_evstrm_t
clone_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;

	return make_evical_vevent(this->ev + this->i, this->nev - this->i);
}

static echs_event_t
next_evical_vevent(echs_evstrm_t s)
{
	struct evical_s *this = (struct evical_s*)s;

	if (UNLIKELY(this->i >= this->nev)) {
		return nul;
	}
	return this->ev[this->i++];
}


struct evrrul_s {
	echs_evstrm_class_t class;
	/* the actual complete vevent (includes proto-event) */
	struct ical_vevent_s ve;
	/* proto-duration */
	echs_idiff_t dur;
	/* iterator state */
	size_t rdi;
	/* unrolled cache */
	size_t ncch;
	echs_instant_t cch[64U];
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
make_evrrul(const struct ical_vevent_s *ve)
{
	struct evrrul_s *res = malloc(sizeof(*res));

	res->class = &evrrul_cls;
	res->ve = *ve;
	res->rdi = 0UL;
	res->ncch = 0UL;
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
	struct evrrul_s *clon = malloc(sizeof(*this));

	*clon = *this;
	return (echs_evstrm_t)clon;
}

/* this should be somewhere else, evrrul.c maybe? */
static size_t
refill(struct evrrul_s *restrict strm)
{
/* useful table at:
 * http://icalevents.com/2447-need-to-know-the-possible-combinations-for-repeating-dates-an-ical-cheatsheet/
 * we're trying to follow that one closely. */
	const struct rrulsp_s *rr;
	echs_instant_t proto;

	if (UNLIKELY((rr = get_grr(strm->ve.rr.r)) == NULL)) {
		return 0UL;
	} else if (UNLIKELY(rr->freq == FREQ_NONE)) {
		return 0UL;
	} else if (strm->ncch) {
		/* preset with last event */
		proto = strm->cch[strm->ncch - 1U];
	} else {
		/* preset with the proto event */
		proto = strm->ve.ev.from;
	}
	/* fill up with the proto instant */

	for (size_t i = 0U; i < countof(strm->cch); i++) {
		strm->cch[i] = proto;
	}

	switch (rr->freq) {
	default:
		break;

	case FREQ_YEARLY:
		/* easiest */
		strm->ncch = rrul_fill_yly(strm->cch, countof(strm->cch), rr);
		break;
	}
	return strm->ncch;
}

static echs_event_t
next_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *restrict this = (struct evrrul_s*)s;
	echs_event_t res;
	echs_instant_t in;

	/* it's easier when we just have some precalc'd rdates */
	if (this->rdi >= this->ncch) {
		/* we have to refill the rdate cache */
		if (refill(this) == 0UL) {
			goto nul;
		}
		/* reset counter */
		this->rdi = 0U;
	}
	/* get the starting instant */
	in = this->cch[this->rdi++];
	/* construct the result */
	res = this->ve.ev;
	res.from = in;
	res.till = echs_instant_add(in, this->dur);
	return res;
nul:
	return nul;
}


#define T	echs_event_t

static inline __attribute__((const, pure)) bool
compare(T e1, T e2)
{
	return echs_instant_lt_p(e1.from, e2.from);
}

#include "wikisort.c"

echs_evstrm_t
make_echs_evical(const char *fn)
{
	vearr_t a;

	if ((a = read_ical(fn)) == NULL) {
		return NULL;
	}
	/* now split into vevents and rrules */
	with (echs_evstrm_t s[a->nev]) {
		echs_event_t ev[a->nev];
		size_t nev = 0UL;
		size_t ns = 0UL;

		/* rearrange so that pure vevents sit in ev,
		 * and rrules somewhere else */
		for (size_t i = 0U; i < a->nev; i++) {
			if (a->ev[i].rr.nr || a->ev[i].rd.ndt) {
				/* it's an rrule, we won't check for
				 * exdates or exrules because they make
				 * no sense without an rrule to go with */
				echs_evstrm_t tmp;

				if ((tmp = make_evrrul(a->ev + i)) != NULL) {
					s[ns++] = tmp;
				}
			} else {
				ev[nev++] = a->ev[i].ev;
			}
		}

		if (nev) {
			/* sort them */
			WikiSort(ev, nev);
			/* and materialise into event stream */
			s[ns++] = make_evical_vevent(ev, nev);
		}
		if (UNLIKELY(!ns)) {
			break;
		} else if (ns == 1UL) {
			return *s;
		}
		return make_echs_evmux(s, ns);
	}
	return NULL;
}

/* evical.c ends here */
