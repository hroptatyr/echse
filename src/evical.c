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
#include <time.h>
#include "evical.h"
#include "intern.h"
#include "state.h"
#include "bufpool.h"
#include "bitint.h"
#include "dt-strpf.h"
#include "evrrul.h"
#include "evmrul.h"
#include "nifty.h"
#include "evical-gp.c"
#include "evrrul-gp.c"
#include "evmrul-gp.c"

#if !defined assert
# define assert(x)
#endif	/* !assert */

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

struct urlst_s {
	size_t nu;
	goptr_t u;
};

struct ical_vevent_s {
	echs_event_t ev;
	/* pointers into the global rrul/xrul array */
	struct rrlst_s rr;
	struct rrlst_s xr;
	/* points into the global xdat/rdat array */
	struct dtlst_s rd;
	struct dtlst_s xd;
	/* points into the global mrul array */
	struct rrlst_s mr;
	/* points into the global mfil array */
	struct urlst_s mf;
};

struct vearr_s {
	size_t nev;
	struct ical_vevent_s ev[];
};

struct uri_s {
	enum {
		URI_NONE,
		URI_FILE,
	} typ;
	obint_t canon;
};

/* mapping from uri to stream */
struct mus_s {
	struct uri_s u;
	echs_evstrm_t s;
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

/* global mrule array */
static size_t zgmr;
static goptr_t ngmr;
static struct mrulsp_s *gmr;

/* global mfile array */
static size_t zgmf;
static goptr_t ngmf;
static struct uri_s *gmf;
/* global mfile streams */
static size_t zgms;
static size_t ngms;
static struct mus_s *gms;

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
add1_to_grr(struct rrulsp_s rr)
{
	goptr_t res;

	CHECK_RESIZE(grr, 16U, 1U);
	grr[res = ngrr++] = rr;
	return res;
}

static goptr_t
clon_grr(struct rrlst_s rl)
{
	const size_t nrr = rl.nr;
	goptr_t res;

	CHECK_RESIZE(grr, 16U, nrr);
	memcpy(grr + (res = ngrr), grr + rl.r, nrr * sizeof(*grr));
	ngrr += nrr;
	return res;
}

static struct rrulsp_s*
get_grr(goptr_t d)
{
	if (UNLIKELY(grr == NULL)) {
		return NULL;
	}
	return grr + d;
}

static goptr_t
add1_to_gxr(struct rrulsp_s xr)
{
	goptr_t res;

	CHECK_RESIZE(gxr, 16U, 1U);
	gxr[res = ngxr++] = xr;
	return res;
}

static goptr_t
clon_gxr(struct rrlst_s rl)
{
	const size_t nxr = rl.nr;
	goptr_t res;

	CHECK_RESIZE(gxr, 16U, nxr);
	memcpy(gxr + (res = ngxr), gxr + rl.r, nxr * sizeof(*gxr));
	ngxr += nxr;
	return res;
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

static echs_instant_t*
get_gxd(goptr_t d)
{
	if (UNLIKELY(gxd == NULL)) {
		return NULL;
	}
	return gxd + d;
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

static goptr_t
add1_to_gmr(struct mrulsp_s mr)
{
	goptr_t res;

	CHECK_RESIZE(gmr, 16U, 1U);
	gmr[res = ngmr++] = mr;
	return res;
}

static struct mrulsp_s*
get_gmr(goptr_t d)
{
	if (UNLIKELY(gmr == NULL)) {
		return NULL;
	}
	return gmr + d;
}

static goptr_t
add1_to_gmf(struct uri_s u)
{
	goptr_t res;

	CHECK_RESIZE(gmf, 16U, 1U);
	gmf[res = ngmf++] = u;
	return res;
}

static goptr_t
add_to_gmf(const struct uri_s *mf, size_t nmf)
{
	goptr_t res;

	CHECK_RESIZE(gmf, 16U, nmf);
	memcpy(gmf + (res = ngmf), mf, nmf * sizeof(*mf));
	ngmf += nmf;
	return res;
}

static struct uri_s*
get_gmf(goptr_t d)
{
	if (UNLIKELY(gmf == NULL)) {
		return NULL;
	}
	return gmf + d;
}

static void
add1_to_gms(struct uri_s u, echs_evstrm_t s)
{
	CHECK_RESIZE(gms, 16U, 1U);
	gms[ngms].u = u;
	gms[ngms].s = s;
	ngms++;
	return;
}

static echs_evstrm_t
get_gms(struct uri_s u)
{
	/* try and find the mfile in the global list */
	for (size_t j = 0U; j < ngms; j++) {
		struct uri_s cu = gms[j].u;
		if (u.typ == cu.typ && u.canon == cu.canon) {
			return gms[j].s;
		}
	}
	return NULL;
}


/* file name stack (and include depth control) */
static const char *gfn[4U];
static size_t nfn;

static int
push_fn(const char *fn)
{
	if (UNLIKELY(nfn >= countof(gfn))) {
		return -1;
	}
	gfn[nfn++] = fn;
	return 0;
}

static const char*
pop_fn(void)
{
	if (UNLIKELY(nfn == 0U)) {
		return NULL;
	}
	return gfn[--nfn];
}

static const char*
peek_fn(void)
{
	if (UNLIKELY(nfn == 0U)) {
		return NULL;
	}
	return gfn[nfn - 1U];
}

static struct uri_s
canon_fn(const char *fn, size_t fz)
{
	obint_t ifn;

	if (*fn != '/') {
		/* relative file name */
		const char *cfn = peek_fn();
		const char *dir;

		if ((dir = strrchr(cfn, '/')) != NULL) {
			char tmp[dir - cfn + fz + 1U/*slash*/ + 1U/*\nul*/];
			char *tp = tmp;

			memcpy(tp, cfn, dir - cfn);
			tp += dir - cfn;
			*tp++ = '/';
			memcpy(tp, fn, fz);
			tp += fz;
			*tp = '\0';
			ifn = intern(tmp, tp - tmp);
			return (struct uri_s){URI_FILE, ifn};
		}
		/* otherwise it was a file name `xyz' in cwd */
	}
	ifn = intern(fn, fz);
	return (struct uri_s){URI_FILE, ifn};
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

static echs_mdir_t
snarf_mdir(const char *spec)
{
	switch (*spec) {
	case 'P':
		/* check 4th character */
		if (spec[4U] == 'T') {
			return MDIR_PASTTHENFUTURE;
		}
		return MDIR_PAST;
	case 'F':
		/* check 6th character */
		if (spec[6U] == 'T') {
			return MDIR_FUTURETHENPAST;
		}
		return MDIR_FUTURE;
	default:
		break;
	}
	return MDIR_NONE;
}

static struct rrulsp_s
snarf_rrule(const char *s, size_t z)
{
	struct rrulsp_s rr = {
		.freq = FREQ_NONE,
		.count = -1U,
		.inter = 1U,
		.until = echs_max_instant(),
	};

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
			if (!(tmp = atol(++kv))) {
				goto bogus;
			}
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
				ass_bi447(&rr.dow, pack_cd(CD(tmp, w)));
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
		case BY_ADD:
			/* these ones take +/- values */
			do {
				tmp = strtol(++kv, &on, 10);
				switch (c->key) {
				case BY_MDAY:
					rr.dom = ass_bi31(rr.dom, tmp);
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
				case BY_ADD:
					ass_bi383(&rr.add, tmp);
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
bogus:
	return (struct rrulsp_s){FREQ_NONE};
}

static struct mrulsp_s
snarf_mrule(const char *s, size_t z)
{
	struct mrulsp_s mr = {
		.mdir = MDIR_NONE,
	};

	for (const char *sp = s, *const ep = s + z, *eofld;
	     sp < ep; sp = eofld + 1) {
		const struct mrul_key_cell_s *c;
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
		if (UNLIKELY((c = __evmrul_key(sp, kz)) == NULL)) {
			/* not found */
			continue;
		}
		/* otherwise do a bit of inspection */
		switch (c->key) {
		case KEY_DIR:
			/* read the spec as well */
			mr.mdir = snarf_mdir(++kv);
			break;

		case KEY_MOVEFROM:
			for (const char *eov; kv < eofld; kv = eov) {
				echs_state_t st;

				eov = strchr(++kv, ',') ?: eofld;
				if (!(st = add_state(kv, eov - kv))) {
					continue;
				}
				/* otherwise assign */
				mr.from = stset_add_state(mr.from, st);
			}
			break;

		case KEY_MOVEINTO:
			for (const char *eov; kv < eofld; kv = eov) {
				echs_state_t st;

				eov = strchr(++kv, ',') ?: eofld;
				if (!(st = add_state(kv, eov - kv))) {
					continue;
				}
				/* otherwise assign */
				mr.into = stset_add_state(mr.into, st);
			}
			break;

		default:
		case MRUL_UNK:
			break;
		}
	}
	return mr;
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
	const char *const ep = line + llen;
	const char *vp;
	const struct ical_fld_cell_s *c;

	if (UNLIKELY((lp = strpbrk(line, ":;")) == NULL)) {
		return;
	} else if (UNLIKELY((c = __evical_fld(line, lp - line)) == NULL)) {
		return;
	}

	/* obtain the value pointer */
	if (LIKELY(*(vp = lp) == ':' || (vp = strchr(lp, ':')) != NULL)) {
		vp++;
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
				if (!echs_nul_instant_p(ve->ev.till)) {
					break;
				}
				/* otherwise also set the TILL slot to I,
				 * as kind of a sane default value */
			case FLD_DTEND:
				ve->ev.till = i;
				break;
			}
		}
		break;
	case FLD_XDATE:
	case FLD_RDATE:
		/* otherwise snarf */
		with (struct dtlst_s l = snarf_dtlst(vp, ep - vp)) {
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
		/* otherwise snarf him */
		for (struct rrulsp_s r;
		     (r = snarf_rrule(vp, ep - vp)).freq != FREQ_NONE;) {
			goptr_t x;

			switch (c->fld) {
			case FLD_RRULE:
				/* bang to global array */
				x = add1_to_grr(r);

				if (!ve->rr.nr++) {
					ve->rr.r = x;
				}
				break;
			case FLD_XRULE:
				/* bang to global array */
				x = add1_to_gxr(r);

				if (!ve->xr.nr++) {
					ve->xr.r = x;
				}
				break;
			}
			/* this isn't supposed to be a for-loop */
			break;
		}
		break;
	case FLD_MRULE:
		/* otherwise snarf him */
		for (struct mrulsp_s r;
		     (r = snarf_mrule(vp, ep - vp)).mdir != MDIR_NONE;) {
			goptr_t x;

			/* bang to global array */
			x = add1_to_gmr(r);

			if (!ve->mr.nr++) {
				ve->mr.r = x;
			}
			/* this isn't supposed to be a for-loop */
			break;
		}
		break;
	case FLD_STATE:
		for (const char *eos; vp < ep; vp = eos + 1U) {
			echs_state_t st;

			eos = strchr(vp, ',') ?: ep;
			st = add_state(vp, eos - vp);
			ve->ev.sts = stset_add_state(ve->ev.sts, st);
		}
		break;
	case FLD_MFILE:
		/* aah, a file-wide MFILE directive */
		if (LIKELY(!strncmp(vp, "file://", 7U))) {
			struct uri_s u = canon_fn(vp += 7U, ep - vp);
			goptr_t x;

			/* bang to global array */
			x = add1_to_gmf(u);

			if (!ve->mf.nu++) {
				ve->mf.u = x;
			}
		}
		break;
	case FLD_UID:
	case FLD_SUMM:
		with (obint_t ob = intern(vp, ep - vp)) {
			switch (c->fld) {
			case FLD_UID:
				ve->ev.uid = ob;
				break;
			case FLD_SUMM:
				ve->ev.sum = ob;
				break;
			}
		}
		break;
	case FLD_DESC:
#if 0
/* we used to have a desc slot, but that went in favour of a uid slot */
		ve->ev.desc = bufpool(vp, ep - vp).str;
#endif	/* 0 */
		break;
	}
	return;
}

static void
snarf_pro(struct ical_vevent_s ve[static 1U], const char *line, size_t llen)
{
/* prologue snarfer */
	const struct ical_fld_cell_s *c;
	const char *lp;

	if (UNLIKELY((lp = strpbrk(line, ":;")) == NULL)) {
		return;
	} else if ((c = __evical_fld(line, lp - line)) == NULL) {
		return;
	}
	/* otherwise inspect the field */
	switch (c->fld) {
	case FLD_MFILE:
		/* aah, a file-wide MFILE directive */
		if (LIKELY(!strncmp(++lp, "file://", 7U))) {
			const char *const ep = line + llen;
			struct uri_s u = canon_fn(lp += 7U, ep -lp);
			goptr_t x;

			/* bang to global array */
			x = add1_to_gmf(u);

			if (!ve->mf.nu++) {
				ve->mf.u = x;
			}
		}
		break;
	default:
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
		ST_BODY,
		ST_VEVENT,
	} st = ST_UNK;
	struct ical_vevent_s ve;
	struct ical_vevent_s globve = {0U};
	size_t nve = 0UL;
	vearr_t a = NULL;

	if (fn == NULL/*stdio*/) {
		fp = stdin;
	} else if ((fp = fopen(fn, "r")) == NULL) {
		return NULL;
	}

	/* let everyone know about our fn */
	push_fn(fn);

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
		/* massage line */
		if (LIKELY(line[nrd - 1U] == '\n')) {
			nrd--;
		}
		if (UNLIKELY(line[nrd - 1U] == '\r')) {
			nrd--;
		}
		line[nrd] = '\0';

		switch (st) {
			static const char beg[] = "BEGIN:VEVENT";
			static const char end[] = "END:VEVENT";

		default:
		case ST_UNK:
			/* check if it's a X-GA-* line, we like those */
			snarf_pro(&globve, line, nrd);
		case ST_BODY:
			/* check if line is a new vevent */
			if (strncmp(line, beg, sizeof(beg) - 1)) {
				/* nope, no state change */
				break;
			}
			/* yep, rinse our bucket */
			memset(&ve, 0, sizeof(ve));
			/* and set state to vevent */
			st = ST_VEVENT;
			break;
		case ST_VEVENT:
			if (strncmp(line, end, sizeof(end) - 1)) {
				/* no state change, interpret the line */
				snarf_fld(&ve, line, nrd);
				break;
			}
			/* otherwise stop parsing and reset the state machine */
			if (a == NULL || nve >= a->nev) {
				/* resize */
				const size_t nu = 2 * nve ?: 64U;
				size_t nz = nu * sizeof(*a->ev);

				a = realloc(a, nz + sizeof(a));
				a->nev = nu;
			}
			/* bang global properties */
			if (globve.mf.nu) {
				const size_t nmf = globve.mf.nu;
				goptr_t mf = globve.mf.u;

				mf = add_to_gmf(get_gmf(mf), nmf);
				if (!ve.mf.nu) {
					ve.mf.u = mf;
				}
				ve.mf.nu += nmf;
			}
			/* assign */
			a->ev[nve++] = ve;
			/* reset to unknown state */
			st = ST_BODY;
			break;
		}
	}
	/* massage result array */
	if (LIKELY(a != NULL)) {
		a->nev = nve;
	}
	/* clean up reader resources */
	free(line);
clo:
	fclose(fp);
	pop_fn();
	return a;
}

static echs_evstrm_t
read_mfil(struct uri_s u)
{
	/* otherwise resort to global reader */
	switch (u.typ) {
		const char *fn;
	case URI_FILE:
		fn = obint_name(u.canon);
		return make_echs_evstrm_from_file(fn);
	default:
		break;
	}
	return NULL;
}

static echs_evstrm_t
get_aux_strm(struct urlst_s ul)
{
	echs_evstrm_t aux[ul.nu];
	size_t naux = 0U;

	for (struct uri_s *u = get_gmf(ul.u), *const eou = u + ul.nu;
	     u < eou; u++) {
		/* try global cache, then mfile reader */
		echs_evstrm_t s;

		if ((s = get_gms(*u)) != NULL) {
			;
		} else if ((s = read_mfil(*u)) != NULL) {
			add1_to_gms(*u, s);
		}
		aux[naux++] = s;
	}
	return echs_evstrm_vmux(aux, naux);
}

static void
prnt_ical_hdr(void)
{
	static time_t now;
	static char stmp[32U];

	puts("BEGIN:VEVENT");
	if (LIKELY(now)) {
		;
	} else {
		struct tm tm[1U];

		if (LIKELY((now = time(NULL), gmtime_r(&now, tm) != NULL))) {
			echs_instant_t nowi;

			nowi.y = tm->tm_year + 1900,
			nowi.m = tm->tm_mon + 1,
			nowi.d = tm->tm_mday,
			nowi.H = tm->tm_hour,
			nowi.M = tm->tm_min,
			nowi.S = tm->tm_sec,
			nowi.ms = ECHS_ALL_SEC,

			dt_strf_ical(stmp, sizeof(stmp), nowi);
		} else {
			/* screw up the singleton */
			now = 0;
			return;
		}
	}
	fputs("DTSTAMP:", stdout);
	puts(stmp);
	return;
}

static void
prnt_ical_ftr(void)
{
	puts("END:VEVENT");
	return;
}

static void
prnt_cd(struct cd_s cd)
{
	static const char *w[] = {
		"MI", "MO", "TU", "WE", "TH", "FR", "SA", "SU"
	};

	if (cd.cnt) {
		fprintf(stdout, "%d", cd.cnt);
	}
	fputs(w[cd.dow], stdout);
	return;
}

static void
prnt_rrul(rrulsp_t rr)
{
	static const char *const f[] = {
		[FREQ_NONE] = "FREQ=NONE",
		[FREQ_YEARLY] = "FREQ=YEARLY",
		[FREQ_MONTHLY] = "FREQ=MONTHLY",
		[FREQ_WEEKLY] = "FREQ=WEEKLY",
		[FREQ_DAILY] = "FREQ=DAILY",
		[FREQ_HOURLY] = "FREQ=HOURLY",
		[FREQ_MINUTELY] = "FREQ=MINUTELY",
		[FREQ_SECONDLY] = "FREQ=SECONDLY",
	};

	fputs(f[rr->freq], stdout);

	if (rr->inter > 1U) {
		fprintf(stdout, ";INTERVAL=%u", rr->inter);
	}
	with (unsigned int m) {
		bitint_iter_t i = 0UL;

		if (!bui31_has_bits_p(rr->mon)) {
			break;
		}
		m = bui31_next(&i, rr->mon);
		fprintf(stdout, ";BYMONTH=%u", m);
		while (m = bui31_next(&i, rr->mon), i) {
			fprintf(stdout, ",%u", m);
		}
	}

	with (int yw) {
		bitint_iter_t i = 0UL;

		if (!bi63_has_bits_p(rr->wk)) {
			break;
		}
		yw = bi63_next(&i, rr->wk);
		fprintf(stdout, ";BYWEEKNO=%d", yw);
		while (yw = bi63_next(&i, rr->wk), i) {
			fprintf(stdout, ",%d", yw);
		}
	}

	with (int yd) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->doy)) {
			break;
		}
		yd = bi383_next(&i, &rr->doy);
		fprintf(stdout, ";BYYEARDAY=%d", yd);
		while (yd = bi383_next(&i, &rr->doy), i) {
			fprintf(stdout, ",%d", yd);
		}
	}

	with (int d) {
		bitint_iter_t i = 0UL;

		if (!bi31_has_bits_p(rr->dom)) {
			break;
		}
		d = bi31_next(&i, rr->dom);
		fprintf(stdout, ";BYMONTHDAY=%d", d);
		while (d = bi31_next(&i, rr->dom), i) {
			fprintf(stdout, ",%d", d);
		}
	}

	with (int e) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->easter)) {
			break;
		}
		e = bi383_next(&i, &rr->easter);
		fprintf(stdout, ";BYEASTER=%d", e);
		while (e = bi383_next(&i, &rr->easter), i) {
			fprintf(stdout, ",%d", e);
		}
	}

	with (struct cd_s cd) {
		bitint_iter_t i = 0UL;

		if (!bi447_has_bits_p(&rr->dow)) {
			break;
		}
		cd = unpack_cd(bi447_next(&i, &rr->dow));
		fputs(";BYDAY=", stdout);
		prnt_cd(cd);
		while (cd = unpack_cd(bi447_next(&i, &rr->dow)), i) {
			fputc(',', stdout);
			prnt_cd(cd);
		}
	}

	with (int a) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->add)) {
			break;
		}
		a = bi383_next(&i, &rr->add);
		fprintf(stdout, ";BYADD=%d", a);
		while (a = bi383_next(&i, &rr->add), i) {
			fprintf(stdout, ",%d", a);
		}
	}

	with (int p) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->pos)) {
			break;
		}
		p = bi383_next(&i, &rr->pos);
		fprintf(stdout, ";BYPOS=%d", p);
		while (p = bi383_next(&i, &rr->pos), i) {
			fprintf(stdout, ",%d", p);
		}
	}

	if ((int)rr->count > 0) {
		fprintf(stdout, ";COUNT=%u", rr->count);
	}
	if (rr->until.u < -1ULL) {
		char until[32U];

		dt_strf_ical(until, sizeof(until), rr->until);
		fprintf(stdout, ";UNTIL=%s", until);
	}

	fputc('\n', stdout);
	return;
}

static void
prnt_stset(echs_stset_t sts)
{
	echs_state_t st = 0U;

	if (UNLIKELY(!sts)) {
		return;
	}
	for (; sts && !(sts & 0b1U); sts >>= 1U, st++);
	fputs(state_name(st), stdout);
	/* print list of states,
	 * we should probably use an iter from state.h here */
	while (st++, sts >>= 1U) {
		for (; sts && !(sts & 0b1U); sts >>= 1U, st++);
		fputc(',', stdout);
		fputs(state_name(st), stdout);
	}
	return;
}

static void
prnt_mrul(mrulsp_t mr)
{
	static const char *const mdirs[] = {
		NULL, "PAST", "PASTTHENFUTURE", "FUTURE", "FUTURETHENPAST",
	};

	if (mr->mdir) {
		fputs("DIR=", stdout);
		fputs(mdirs[mr->mdir], stdout);
	}
	if (mr->from) {
		fputs(";MOVEFROM=", stdout);
		prnt_stset(mr->from);
	}
	if (mr->into) {
		fputs(";MOVEINTO=", stdout);
		prnt_stset(mr->into);
	}

	fputc('\n', stdout);
	return;
}

static void
prnt_ev(echs_event_t ev)
{
	static unsigned int auto_uid;
	char stmp[32U] = {':'};

	if (UNLIKELY(echs_nul_instant_p(ev.from))) {
		return;
	}
	dt_strf_ical(stmp + 1U, sizeof(stmp) - 1U, ev.from);
	fputs("DTSTART", stdout);
	if (echs_instant_all_day_p(ev.from)) {
		fputs(";VALUE=DATE", stdout);
	}
	puts(stmp);

	if (LIKELY(!echs_nul_instant_p(ev.till))) {
		dt_strf_ical(stmp + 1U, sizeof(stmp) - 1U, ev.till);
	} else {
		ev.till = ev.from;
	}
	fputs("DTEND", stdout);
	if (echs_instant_all_day_p(ev.till)) {
		fputs(";VALUE=DATE", stdout);
	}
	puts(stmp);

	/* fill in a missing uid? */
	auto_uid++;
	if (ev.uid) {
		fputs("UID:", stdout);
		puts(obint_name(ev.uid));
	} else {
		/* it's mandatory, so generate one */
		fprintf(stdout, "UID:echse_merged_vevent_%u\n", auto_uid);
	}
	if (ev.sum) {
		fputs("SUMMARY:", stdout);
		puts(obint_name(ev.sum));
	}
	if (ev.sts) {
		fputs("X-GA-STATE:", stdout);
		prnt_stset(ev.sts);
		fputc('\n', stdout);
	}
	return;
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
static void prnt_evical_vevent(echs_evstrm_t);

static const struct echs_evstrm_class_s evical_cls = {
	.next = next_evical_vevent,
	.free = free_evical_vevent,
	.clone = clone_evical_vevent,
	.prnt1 = prnt_evical_vevent,
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

static void
prnt_evical_vevent(echs_evstrm_t s)
{
	const struct evical_s *this = (struct evical_s*)s;

	for (size_t i = 0UL; i < this->nev; i++) {
		prnt_ical_hdr();
		prnt_ev(this->ev[i]);
		prnt_ical_ftr();
	}
	return;
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
static void prnt_evrrul1(echs_evstrm_t);
static void prnt_evrrulm(const echs_evstrm_t s[], size_t n);

static const struct echs_evstrm_class_s evrrul_cls = {
	.next = next_evrrul,
	.free = free_evrrul,
	.clone = clone_evrrul,
	.prnt1 = prnt_evrrul1,
	.prntm = prnt_evrrulm,
};

static echs_evstrm_t
__make_evrrul(const struct ical_vevent_s *ve)
{
	struct evrrul_s *res = malloc(sizeof(*res));

	res->class = &evrrul_cls;
	res->ve = *ve;
	res->dur = echs_instant_diff(ve->ev.till, ve->ev.from);
	res->rdi = 0UL;
	res->ncch = 0UL;
	if (ve->mr.nr && ve->mf.nu) {
		mrulsp_t mr = get_gmr(ve->mr.r);
		echs_evstrm_t aux;

		if (LIKELY((aux = get_aux_strm(ve->mf)) != NULL)) {
			return make_evmrul(mr, (echs_evstrm_t)res, aux);
		}
		/* otherwise display stream as is, maybe print a warning? */
	}
	return (echs_evstrm_t)res;
}

static echs_evstrm_t
make_evrrul(const struct ical_vevent_s *ve)
{
	switch (ve->rr.nr) {
	case 0:
		return NULL;
	case 1:
		return __make_evrrul(ve);
	default:
		break;
	}
	with (echs_evstrm_t s[ve->rr.nr]) {
		size_t nr = 0UL;

		for (size_t i = 0U; i < ve->rr.nr; i++) {
			struct ical_vevent_s ve_tmp = *ve;

			ve_tmp.rr.r += i;
			ve_tmp.rr.nr = 1U;
			s[nr++] = __make_evrrul(&ve_tmp);
		}
		return make_echs_evmux(s, nr);
	}
	return NULL;
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
	/* we have to clone rrules and exrules though */
	if (this->ve.rr.nr) {
		clon->ve.rr.r = clon_grr(this->ve.rr);
	}
	if (this->ve.xr.nr) {
		clon->ve.xr.r = clon_gxr(this->ve.xr);
	}
	return (echs_evstrm_t)clon;
}

/* this should be somewhere else, evrrul.c maybe? */
static size_t
refill(struct evrrul_s *restrict strm)
{
/* useful table at:
 * http://icalevents.com/2447-need-to-know-the-possible-combinations-for-repeating-dates-an-ical-cheatsheet/
 * we're trying to follow that one closely. */
	struct rrulsp_s *restrict rr;

	assert(strm->vr.rr.nr > 0UL);
	if (UNLIKELY((rr = get_grr(strm->ve.rr.r)) == NULL)) {
		return 0UL;
	} else if (UNLIKELY(!rr->count)) {
		return 0UL;
	}

	/* fill up with the proto instant */
	for (size_t j = 0U; j < countof(strm->cch); j++) {
		strm->cch[j] = strm->ve.ev.from;
	}

	/* now go and see who can help us */
	switch (rr->freq) {
	default:
		strm->ncch = 0UL;
		break;

	case FREQ_YEARLY:
		/* easiest */
		strm->ncch = rrul_fill_yly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_MONTHLY:
		/* second easiest */
		strm->ncch = rrul_fill_mly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_WEEKLY:
		strm->ncch = rrul_fill_wly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_DAILY:
		strm->ncch = rrul_fill_dly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_HOURLY:
		strm->ncch = rrul_fill_Hly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_MINUTELY:
		strm->ncch = rrul_fill_Mly(strm->cch, countof(strm->cch), rr);
		break;
	case FREQ_SECONDLY:
		strm->ncch = rrul_fill_Sly(strm->cch, countof(strm->cch), rr);
		break;
	}

	if (strm->ncch < countof(strm->cch)) {
		rr->count = 0;
	} else {
		/* update the rrule to the partial set we've got */
		rr->count -= --strm->ncch;
		strm->ve.ev.from = strm->cch[strm->ncch];
	}
	if (UNLIKELY(strm->ncch == 0UL)) {
		return 0UL;
	}
	/* otherwise sort the array, just in case */
	echs_instant_sort(strm->cch, strm->ncch);

	/* also check if we need to rid some dates due to xdates */
	if (UNLIKELY(strm->ve.xd.ndt > 0UL)) {
		const echs_instant_t *xd = get_gxd(strm->ve.xd.dt);
		echs_instant_t *restrict rd = strm->cch;
		const echs_instant_t *const exd = xd + strm->ve.xd.ndt;
		const echs_instant_t *const erd = rd + strm->ncch;

		assert(xd != NULL);
		for (; xd < exd; xd++, rd++) {
			/* fast forward to xd (we assume it's sorted) */
			for (; rd < erd && echs_instant_lt_p(*rd, *xd); rd++);
			/* now we're either on xd or past it */
			if (echs_instant_eq_p(*rd, *xd)) {
				/* leave rd out then */
				*rd = echs_nul_instant();
			}
		}
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
	refill:
		/* we have to refill the rdate cache */
		if (refill(this) == 0UL) {
			goto nul;
		}
		/* reset counter */
		this->rdi = 0U;
	}
	/* get the starting instant */
	while (UNLIKELY(echs_instant_0_p(in = this->cch[this->rdi++]))) {
		/* we might have run out of steam */
		if (UNLIKELY(this->rdi >= this->ncch)) {
			goto refill;
		}
	}
	/* construct the result */
	res = this->ve.ev;
	res.from = in;
	res.till = echs_instant_add(in, this->dur);
	return res;
nul:
	return nul;
}

static void
prnt_evrrul1(echs_evstrm_t s)
{
	const struct evrrul_s *this = (struct evrrul_s*)s;

	prnt_ical_hdr();
	prnt_ev(this->ve.ev);

	for (size_t i = 0UL; i < this->ve.rr.nr; i++) {
		rrulsp_t rr = get_grr(this->ve.rr.r + i);

		fputs("RRULE:", stdout);
		prnt_rrul(rr);
	}
	for (size_t i = 0UL; i < this->ve.mr.nr; i++) {
		mrulsp_t mr = get_gmr(this->ve.mr.r + i);

		fputs("X-GA-MRULE:", stdout);
		prnt_mrul(mr);
	}
	prnt_ical_ftr();
	return;
}

static void
prnt_evrrulm(const echs_evstrm_t s[], size_t n)
{
/* we know that they all come from one ical_event_s originally,
 * just merge them temporarily in a evrrul_s object and use prnt1() */
	struct evrrul_tmp_s {
		echs_evstrm_class_t class;
		/* the actual complete vevent (includes proto-event) */
		struct ical_vevent_s ve;
		/* proto-duration */
		echs_idiff_t dur;
	};
	struct evrrul_tmp_s this = *(struct evrrul_tmp_s*)*s;

	/* make sure we talk about a split up VEVENT with multiple rules */
	for (size_t i = 1U; i < n; i++) {
		const struct evrrul_tmp_s *that = (const void*)s[i];
		if (!echs_event_eq_p(this.ve.ev, that->ve.ev)) {
			goto one_by_one;
		}
	}
	/* have to fiddle with the nr slot, but we know rules are consecutive */
	this.ve.rr.nr = n;
	prnt_evrrul1((echs_evstrm_t)&this);
	return;

one_by_one:
	for (size_t i = 0U; i < n; i++) {
		prnt_evrrul1(s[i]);
	}
	return;
}


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
			const struct ical_vevent_s *ve = a->ev + i;
			echs_evstrm_t tmp;
			echs_instant_t *xd;

			if (!ve->rr.nr && !ve->rd.ndt) {
				/* not an rrule but a normal vevent
				 * just him to the list */
				ev[nev++] = a->ev[i].ev;
				continue;
			}
			/* it's an rrule, we won't check for
			 * exdates or exrules because they make
			 * no sense without an rrule to go with */
			/* check for exdates here, and sort them */
			if (UNLIKELY(ve->xd.ndt > 1UL &&
				     (xd = get_gxd(ve->xd.dt)) != NULL)) {
				echs_instant_sort(xd, ve->xd.ndt);
			}

			if ((tmp = make_evrrul(a->ev + i)) != NULL) {
				s[ns++] = tmp;
			}
		}

		if (nev) {
			/* sort them */
			echs_event_sort(ev, nev);
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

void
echs_prnt_ical_event(echs_event_t ev)
{
	prnt_ical_hdr();
	prnt_ev(ev);
	prnt_ical_ftr();
	return;
}

void
echs_prnt_ical_init(void)
{
	fputs("\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
CALSCALE:GREGORIAN\n", stdout);
	return;
}

void
echs_prnt_ical_fini(void)
{
	fputs("\
END:VCALENDAR\n", stdout);
	return;
}

struct rrulsp_s
echs_read_rrul(const char *s, size_t z)
{
	return snarf_rrule(s, z);
}

/* evical.c ends here */
