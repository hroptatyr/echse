/*** evical.c -- simple icalendar parser for echse
 *
 * Copyright (C) 2013-2015 Sebastian Freundt
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
#include <fcntl.h>
#include "evical.h"
#include "task.h"
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

struct uri_s {
	enum {
		URI_NONE,
		URI_FILE,
	} typ;
	obint_t canon;
};

struct dtlst_s {
	echs_instant_t *dt;
	size_t ndt;
	size_t zdt;
};

struct rrlst_s {
	struct rrulsp_s *r;
	size_t nr;
	size_t zr;
};

struct mrlst_s {
	struct mrulsp_s *r;
	size_t nr;
	size_t zr;
};

struct urlst_s {
	struct uri_s *u;
	size_t nu;
	size_t zu;
};

struct ical_vevent_s {
	echs_event_t e;

	/* proto typical task */
	struct echs_task_s t;

	/* pointers into the rrul/xrul array */
	struct rrlst_s rr;
	struct rrlst_s xr;
	/* points into the xdat/rdat array */
	struct dtlst_s rd;
	struct dtlst_s xd;
	/* points into the mrul array */
	struct mrlst_s mr;
	/* points into the mfil array */
	struct urlst_s mf;
};

struct vearr_s {
	size_t nev;
	struct ical_vevent_s ev[];
};

/* mapping from uri to stream */
struct mus_s {
	struct uri_s u;
	echs_evstrm_t s;
};

struct muslst_s {
	struct mus_s *us;
	size_t nus;
	size_t zus;
};

/* generic string, here to denote a cal-address */
struct cal_addr_s {
	const char *s;
	size_t z;
};

static struct muslst_s gms;


#define CHECK_RESIZE(o, id, iniz, nitems)				\
	if (UNLIKELY(!(o)->z##id)) {					\
		/* leave the first one out */				\
		(o)->id = malloc(((o)->z##id = iniz) * sizeof(*(o)->id)); \
		memset((o)->id, 0, sizeof(*(o)->id));			\
	}								\
	if (UNLIKELY((o)->n##id + nitems > (o)->z##id)) {		\
		do {							\
			(o)->id = realloc(				\
				(o)->id,				\
				((o)->z##id *= 2U) * sizeof(*(o)->id)); \
		} while ((o)->n##id + nitems > (o)->z##id);		\
	}

static void
add1_to_rrlst(struct rrlst_s *rl, struct rrulsp_s rr)
{
	CHECK_RESIZE(rl, r, 16U, 1U);
	rl->r[rl->nr++] = rr;
	return;
}

static struct rrlst_s
clon_rrlst(struct rrlst_s rl)
{
	struct rrlst_s res = rl;

	res.r = malloc(rl.zr * sizeof(*rl.r));
	memcpy(res.r, rl.r, rl.nr * sizeof(*rl.r));
	return res;
}

static void
add1_to_mrlst(struct mrlst_s *rl, struct mrulsp_s mr)
{
	CHECK_RESIZE(rl, r, 16U, 1U);
	rl->r[rl->nr++] = mr;
	return;
}

static void
add1_to_dtlst(struct dtlst_s *dl, echs_instant_t dt)
{
	CHECK_RESIZE(dl, dt, 64U, 1);
	dl->dt[dl->ndt++] = dt;
	return;
}

static struct dtlst_s
clon_dtlst(struct dtlst_s dl)
{
	struct dtlst_s res = dl;

	res.dt = malloc(dl.zdt * sizeof(*dl.dt));
	memcpy(res.dt, dl.dt, dl.ndt * sizeof(*dl.dt));
	return res;
}

static void
add1_to_urlst(struct urlst_s *ul, struct uri_s u)
{
	CHECK_RESIZE(ul, u, 16U, 1U);
	ul->u[ul->nu++] = u;
	return;
}

static void
add_to_urlst(struct urlst_s *ul, const struct uri_s *mf, size_t nmf)
{
	CHECK_RESIZE(ul, u, 16U, nmf);
	memcpy(ul->u + ul->nu, mf, nmf * sizeof(*mf));
	ul->nu += nmf;
	return;
}


static void
add1_to_gms(struct uri_s u, echs_evstrm_t s)
{
	CHECK_RESIZE(&gms, us, 16U, 1U);
	gms.us[gms.nus++] = (struct mus_s){u, s};
	return;
}

static echs_evstrm_t
get_gms(struct uri_s u)
{
	/* try and find the mfile in the global list */
	for (size_t j = 0U; j < gms.nus; j++) {
		struct uri_s cu = gms.us[j].u;
		if (u.typ == cu.typ && u.canon == cu.canon) {
			return gms.us[j].s;
		}
	}
	return NULL;
}

static void
make_proto_task(struct ical_vevent_s *restrict ve)
{
	struct echs_task_s *res = malloc(sizeof(*res));

	if (UNLIKELY(res == NULL)) {
		return;
	}
	/* copy the proto task over */
	*res = ve->t;
	ve->e.task = res;
	return;
}

static void
free_ical_vevent(struct ical_vevent_s *restrict ve)
{
	if (ve->rr.nr) {
		free(ve->rr.r);
	}
	if (ve->rd.ndt) {
		free(ve->rd.dt);
	}
	if (ve->xr.nr) {
		free(ve->xr.r);
	}
	if (ve->xd.ndt) {
		free(ve->xd.dt);
	}
	if (ve->mf.nu) {
		free(ve->mf.u);
	}
	if (ve->mr.nr) {
		free(ve->mr.r);
	}
	return;
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
			on = NULL;
			do {
				echs_wday_t w;

				tmp = strtol(++kv, &on, 10);
				if ((w = snarf_wday(on)) == MIR) {
					continue;
				}
				/* otherwise assign */
				ass_bi447(&rr.dow, pack_cd(CD(tmp, w)));
			} while (on && (kv = strchr(on, ',')) != NULL);
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
	struct dtlst_s dl = {NULL};

	for (const char *sp = line, *const ep = line + llen, *eod;
	     sp < ep; sp = eod + 1U) {
		echs_instant_t in;

		if (UNLIKELY((eod = strchr(sp, ',')) == NULL)) {
			eod = ep;
		}
		if (UNLIKELY(echs_instant_0_p(in = dt_strp(sp)))) {
			continue;
		}
		add1_to_dtlst(&dl, in);
	}
	return dl;
}

static struct cal_addr_s
snarf_mailto(const char *line, size_t llen)
{
	static const char prfx[] = "mailto:";

	if (llen < strlenof(prfx) || memchr(line, ':', llen) == NULL) {
		return (struct cal_addr_s){line, llen};
	} else if (LIKELY(!strncmp(line, prfx, strlenof(prfx)))) {
		/* brill */
		return (struct cal_addr_s){
			line + strlenof(prfx), llen - strlenof(prfx)
		};
	}
	return (struct cal_addr_s){NULL};
}

static int
snarf_fld(struct ical_vevent_s ve[static 1U], const char *line, size_t llen)
{
	const char *lp;
	const char *const ep = line + llen;
	const char *vp;
	const struct ical_fld_cell_s *c;

	if (UNLIKELY((lp = strpbrk(line, ":;")) == NULL)) {
		return -1;
	} else if (UNLIKELY((c = __evical_fld(line, lp - line)) == NULL)) {
		return -1;
	}

	/* obtain the value pointer */
	if (LIKELY(*(vp = lp) == ':' || (vp = strchr(lp, ':')) != NULL)) {
		vp++;
	} else {
		return -1;
	}

	switch (c->fld) {
	default:
	case FLD_UNK:
		/* how did we get here */
		return -1;
	case FLD_DTSTART:
	case FLD_DTEND:
		with (echs_instant_t i = snarf_value(lp)) {
			switch (c->fld) {
			case FLD_DTSTART:
				ve->e.from = i;
				if (!echs_nul_instant_p(ve->e.till)) {
					break;
				}
				/* otherwise also set the TILL slot to I,
				 * as kind of a sane default value */
			case FLD_DTEND:
				ve->e.till = i;
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
			case FLD_XDATE:
				ve->xd = l;
				break;
			case FLD_RDATE:
				ve->rd = l;
				break;
			}
		}
		break;
	case FLD_RRULE:
	case FLD_XRULE:
		/* otherwise snarf him */
		if_with (struct rrulsp_s r,
			 (r = snarf_rrule(vp, ep - vp)).freq != FREQ_NONE) {
			switch (c->fld) {
			case FLD_RRULE:
				/* bang to global array */
				add1_to_rrlst(&ve->rr, r);
				break;
			case FLD_XRULE:
				/* bang to global array */
				add1_to_rrlst(&ve->xr, r);
				break;
			}
		}
		break;
	case FLD_MRULE:
		/* otherwise snarf him */
		if_with (struct mrulsp_s r,
			 (r = snarf_mrule(vp, ep - vp)).mdir != MDIR_NONE) {
			/* bang to global array */
			add1_to_mrlst(&ve->mr, r);
		}
		break;
	case FLD_STATE:
		for (const char *eos; vp < ep; vp = eos + 1U) {
			echs_state_t st;

			eos = strchr(vp, ',') ?: ep;
			st = add_state(vp, eos - vp);
			ve->e.sts = stset_add_state(ve->e.sts, st);
		}
		break;
	case FLD_MFILE:
		/* aah, a file-wide MFILE directive */
		if (LIKELY(!strncmp(vp, "file://", 7U))) {
			struct uri_s u = (vp += 7U, canon_fn(vp, ep - vp));

			/* bang to global array */
			add1_to_urlst(&ve->mf, u);
		}
		break;
	case FLD_UID:
		ve->t.uid = intern(vp, ep - vp);
		break;
	case FLD_SUMM:
		if (ve->t.cmd != NULL) {
			/* only the first summary wins */
			break;
		}
		if (vp < ep) {
			ve->t.cmd = strndup(vp, ep - vp);
		}
		break;

	case FLD_DESC:
		break;

	case FLD_LOC:
		if (ve->t.run_as.wd != NULL) {
			/* only the first location wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.run_as.wd = strndup(vp, ep - vp);
		break;

	case FLD_ATT:
		/* snarf mail address */
		if_with (struct cal_addr_s a,
			 (a = snarf_mailto(vp, ep - vp)).s) {
			/* bang straight into the proto task */
			strlst_addn(&ve->t.att, a.s, a.z);
		}
		break;

	case FLD_ORG:
		if (ve->t.org != NULL) {
			/* only the first organiser wins */
			break;
		}
		if_with (struct cal_addr_s a,
			 (a = snarf_mailto(vp, ep - vp)).s) {
			/* bang straight into the proto task */
			ve->t.org = strndup(a.s, a.z);
		}
		break;
	}
	return 0;
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
			struct uri_s u = (lp += 7U, canon_fn(lp, ep - lp));

			/* bang to global array */
			add1_to_urlst(&ve->mf, u);
		}
		break;
	default:
		break;
	}
	return;
}


/* ical parsers, push and pull */
struct ical_parser_s {
	enum {
		ST_UNK,
		ST_BODY,
		ST_VEVENT,
	} st;
	struct ical_vevent_s ve;
	struct ical_vevent_s globve;

	/* the input buffer we're currently working on */
	const char *buf;
	size_t bsz;
	size_t bix;

	size_t six;
	char stash[1024U];
};

#define ICAL_EOP	((struct ical_vevent_s*)0x1U)

static size_t
esccpy(char *restrict tgt, size_t tz, const char *src, size_t sz)
{
	size_t ti = 0U;

	for (size_t si = 0U; si < sz; si++) {
		switch ((tgt[ti] = src[si])) {
		case '\r':
			break;
		case '\n':
			/* overread along with the next space */
			si++;
			break;
		case '\\':
			/* ah, one of them escape sequences */
			switch (src[si]) {
			case 'n':
			case 'N':
				tgt[ti++] = '\n';
				si++;
				break;
			case '"':
			case ';':
			case ',':
			case '\\':
			default:
				tgt[ti++] = src[si++];
				break;
			}
			break;
		case '"':
			/* grrr, these need escaping too innit? */
		default:
			ti++;
			break;
		}
		/* not sure what to do with long lines */
		if (UNLIKELY(ti >= tz)) {
			/* ignore them */
			return 0U;
		}
	}
	tgt[ti] = '\0';
	return ti;
}

static int
_ical_init_push(const char *buf, size_t bsz)
{
	/* oh they're here for the first time, check
	 * the first 16 bytes */
	static const char hdr[] = "BEGIN:VCALENDAR";

	if (UNLIKELY(buf == NULL)) {
		/* huh? no buffer AND no context? */
		return -1;
	}

	/* little probe first
	 * luckily BEGIN:VCALENDAR\n is exactly 16 bytes */
	if (bsz < sizeof(hdr) ||
	    memcmp(buf, hdr, strlenof(hdr)) ||
	    !(buf[strlenof(hdr)] == '\n' ||
	      buf[strlenof(hdr)] == '\r')) {
		/* that's just rubbish, refuse to do anything */
		return -1;
	}
	return 0;
}

static void
_ical_push(struct ical_parser_s p[static 1U], const char *buf, size_t bsz)
{
	p->buf = buf;
	p->bsz = bsz;
	p->bix = 0U;
	return;
}

static struct ical_vevent_s*
_ical_proc(struct ical_parser_s p[static 1U])
{
/* parse stuff in P->stash up to a size of SZ
 * the stash will contain a whole line which is assumed to be consumed */
	const char *sp = p->stash;
	const size_t sz = p->six;
	struct ical_vevent_s *res = NULL;

	switch (p->st) {
		static const char beg[] = "BEGIN:VEVENT";
		static const char end[] = "END:VEVENT";

	default:
	case ST_UNK:
		/* check if it's a X-GA-* line, we like those */
		snarf_pro(&p->globve, sp, sz);
		/*@fallthrough@*/
	case ST_BODY:
		/* check if line is a new vevent */
		if (sz >= strlenof(beg) && !strncmp(sp, beg, strlenof(beg))) {
			/* yep, rinse our bucket */
			memset(&p->ve, 0, sizeof(p->ve));
			/* and set state to vevent */
			p->st = ST_VEVENT;
		}
		break;

	case ST_VEVENT:
		if (sz >= strlenof(end) && !strncmp(sp, end, strlenof(end))) {
			/* yep, prepare to report success */
			if (p->globve.mf.nu) {
				const size_t nmf = p->globve.mf.nu;
				const struct uri_s *mf = p->globve.mf.u;

				add_to_urlst(&p->ve.mf, mf, nmf);
			}
			/* assign */
			make_proto_task(&p->ve);
			/* reset to unknown state */
			p->st = ST_BODY;
			res = &p->ve;
			/* success */
			break;
		}
		/* no state change, interpret the line */
		if (snarf_fld(&p->ve, sp, sz) < 0) {
			;
		}
		break;
	}
	/* we've consumed him */
	p->six = 0U;
	return res;
}

static struct ical_vevent_s*
_ical_pull(struct ical_parser_s p[static 1U])
{
/* pull-version of read_ical */
	static const char ftr[] = "END:VCALENDAR";
	struct ical_vevent_s *res = NULL;
	const char *eol;

#define BP	(p->buf + p->bix)
#define BZ	(p->bsz - p->bix)
#define BI	(p->bix)
chop_more:
	/* chop _p->buf into lines (possibly multilines) */
	for (const char *tmp = BP, *const ep = BP + BZ;
	     (eol = memchr(tmp, '\n', ep - tmp)) != NULL &&
		     ++eol < ep && (*eol == ' ' || *eol == '\t'); tmp = eol);
	if (UNLIKELY((eol == NULL || eol >= BP + BZ) &&
		     BZ >= sizeof(p->stash) - p->six)) {
		/* we must have stopped mid-stream at the end of the buffer
		 * however, our stash space is too small to hold the contents
		 * we'll just fuck off and hope nobody will notice */
		p->six = 0U;
	} else if (UNLIKELY(eol == NULL || eol >= BP + BZ)) {
		/* copy what we've got to the stash for small buffers */
		char *restrict sp = p->stash + p->six;
		size_t sz = sizeof(p->stash) - p->six;

		p->six += esccpy(sp, sz, BP, BZ);
	} else if (UNLIKELY(eol >= BP + strlenof(ftr) &&
			    !strncmp(BP, ftr, strlenof(ftr)))) {
		/* oooh it's the end of the whole shebang,
		 * indicate end-of-parsing */
		res = ICAL_EOP;
	} else {
		const char *bp = BP;
		const size_t llen = eol - bp;
		char *restrict sp = p->stash + p->six;
		size_t slen = sizeof(p->stash) - p->six;

		/* ... pretend we've consumed it all */
		BI += llen;

		/* copy to stash and unescape */
		slen = esccpy(sp, slen, bp, llen);
		/* store new stash pointer */
		p->six += slen;

		if (p->six && (res = _ical_proc(p)) == NULL) {
			goto chop_more;
		}
	}
#undef BP
#undef BZ
#undef BI
	return res;
}

static void
_ical_fini(struct ical_parser_s p[static 1U])
{
/* parser dtor
 * when we're still in state VEVENT then something
 * must have terribly go wrong and the ve resources
 * need cleaning up as well, otherwise assume our
 * ctors (make_evrrul(), etc.) free unneeded resources */

	if (UNLIKELY(p->st == ST_VEVENT)) {
		free_ical_vevent(&p->ve);
	}
	/* free the globve */
	free_ical_vevent(&p->globve);
	/* dissolve all of it */
	memset(p, 0, sizeof(*p));
	return;
}

static vearr_t
read_ical(const char *fn)
{
	char buf[65536U];
	size_t nve = 0UL;
	vearr_t a = NULL;
	struct ical_parser_s pp = {.buf = NULL};
	ssize_t nrd;
	int fd;

	if (fn == NULL/*stdio*/) {
		fd = STDIN_FILENO;
	} else if ((fd = open(fn, O_RDONLY)) < 0) {
		return NULL;
	}

	/* let everyone know about our fn */
	push_fn(fn);

redo:
	switch ((nrd = read(fd, buf, sizeof(buf)))) {
		struct ical_vevent_s *ve;

	default:
		if (UNLIKELY(pp.buf == NULL && _ical_init_push(buf, nrd) < 0)) {
			/* buffer completely unsuitable for pushing */
			break;
		}

		_ical_push(&pp, buf, nrd);
	case 0:
		while ((ve = _ical_pull(&pp)) != NULL) {
			if (UNLIKELY(ve == ICAL_EOP)) {
				/* oh jolly good */
				goto undo;
			}
			/* just add to vearray */
			if (a == NULL || nve >= a->nev) {
				/* resize */
				const size_t nu = 2 * nve ?: 64U;
				size_t nz = nu * sizeof(*a->ev);

				a = realloc(a, nz + sizeof(a));
				a->nev = nu;
			}
			/* assign */
			a->ev[nve++] = *ve;
		}
		if (LIKELY(nrd > 0)) {
			goto redo;
		}
		/*@fallthrough@*/
	undo:
	case -1:
		_ical_fini(&pp);
		break;
	}

	/* massage result array */
	if (LIKELY(a != NULL)) {
		a->nev = nve;
	}

	close(fd);
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

	for (struct uri_s *u = ul.u, *const eou = u + ul.nu; u < eou; u++) {
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
		struct tm tm;

		if (LIKELY((now = time(NULL), gmtime_r(&now, &tm) != NULL))) {
			echs_instant_t nowi;

			nowi.y = tm.tm_year + 1900,
			nowi.m = tm.tm_mon + 1,
			nowi.d = tm.tm_mday,
			nowi.H = tm.tm_hour,
			nowi.M = tm.tm_min,
			nowi.S = tm.tm_sec,
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

	if (mr.mdir) {
		fputs("DIR=", stdout);
		fputs(mdirs[mr.mdir], stdout);
	}
	if (mr.from) {
		fputs(";MOVEFROM=", stdout);
		prnt_stset(mr.from);
	}
	if (mr.into) {
		fputs(";MOVEINTO=", stdout);
		prnt_stset(mr.into);
	}

	fputc('\n', stdout);
	return;
}

static void
prnt_ev(echs_event_t e)
{
	static unsigned int auto_uid;
	char stmp[32U] = {':'};

	if (UNLIKELY(echs_nul_instant_p(e.from))) {
		return;
	}
	dt_strf_ical(stmp + 1U, sizeof(stmp) - 1U, e.from);
	fputs("DTSTART", stdout);
	if (echs_instant_all_day_p(e.from)) {
		fputs(";VALUE=DATE", stdout);
	}
	puts(stmp);

	if (LIKELY(!echs_nul_instant_p(e.till))) {
		dt_strf_ical(stmp + 1U, sizeof(stmp) - 1U, e.till);
	} else {
		e.till = e.from;
	}
	fputs("DTEND", stdout);
	if (echs_instant_all_day_p(e.till)) {
		fputs(";VALUE=DATE", stdout);
	}
	puts(stmp);

	/* fill in a missing uid? */
	auto_uid++;
	if (e.task->uid) {
		fputs("UID:", stdout);
		puts(obint_name(e.task->uid));
	} else {
		/* it's mandatory, so generate one */
		fprintf(stdout, "UID:echse_merged_vevent_%u\n", auto_uid);
	}
	if (e.task->cmd) {
		fputs("SUMMARY:", stdout);
		puts(e.task->cmd);
	}
	if (e.task->org) {
		fputs("ORGANIZER:", stdout);
		puts(e.task->org);
	}
	if (e.task->att) {
		for (const char *const *ap = e.task->att->l; *ap; ap++) {
			fputs("ATTENDEE:", stdout);
			puts(*ap);
		}
	}
	if (e.sts) {
		fputs("X-GA-STATE:", stdout);
		prnt_stset(e.sts);
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
static echs_evstrm_t clone_evical_vevent(echs_const_evstrm_t);
static void prnt_evical_vevent(echs_const_evstrm_t);

static const struct echs_evstrm_class_s evical_cls = {
	.next = next_evical_vevent,
	.free = free_evical_vevent,
	.clone = clone_evical_vevent,
	.prnt1 = prnt_evical_vevent,
};

static const echs_event_t nul;

static echs_evstrm_t
make_evical_vevent(const struct echs_event_s *ev, size_t nev)
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

	for (size_t i = 0U; i < this->nev; i++) {
		if (this->ev[i].task) {
			free_echs_task(this->ev[i].task);
		}
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evical_vevent(echs_const_evstrm_t s)
{
	const struct evical_s *this = (const struct evical_s*)s;
	struct evical_s *res;

	res = (struct evical_s*)make_evical_vevent(
		this->ev + this->i, this->nev - this->i);
	for (size_t i = 0U; i < this->nev - this->i; i++) {
		/* increment task ref-counter */
		res->ev[i].task = echs_task_clone(res->ev[i].task);
	}
	return (echs_evstrm_t)res;
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
prnt_evical_vevent(echs_const_evstrm_t s)
{
	const struct evical_s *this = (const struct evical_s*)s;

	for (size_t i = 0UL; i < this->nev; i++) {
		prnt_ical_hdr();
		prnt_ev(this->ev[i]);
		prnt_ical_ftr();
	}
	return;
}


struct evrrul_s {
	echs_evstrm_class_t class;

	/* proto-event */
	echs_event_t e;

	/* reference counter */
	size_t nref;

	/* rrul/xrul */
	struct rrulsp_s rr;
	struct rrlst_s xr;
	/* rdat/xdat */
	struct dtlst_s rd;
	struct dtlst_s xd;

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
static echs_evstrm_t clone_evrrul(echs_const_evstrm_t);
static void prnt_evrrul1(echs_const_evstrm_t);
static void prnt_evrrulm(const echs_const_evstrm_t s[], size_t n);

static const struct echs_evstrm_class_s evrrul_cls = {
	.next = next_evrrul,
	.free = free_evrrul,
	.clone = clone_evrrul,
	.prnt1 = prnt_evrrul1,
	.prntm = prnt_evrrulm,
};

static echs_evstrm_t
__make_evrrul(struct ical_vevent_s ve, size_t nref)
{
	struct evrrul_s *this = malloc(sizeof(*this));
	echs_evstrm_t res;

	this->class = &evrrul_cls;
	this->e = ve.e;
	this->rr = *ve.rr.r;
	this->nref = nref;

	/* free the rr-list in here after materialisation */
	if (nref <= 1U) {
		free(ve.rr.r);
	}

	if (ve.xr.nr) {
		this->xr = ve.xr;
	} else {
		this->xr.nr = 0U;
	}
	if (ve.rd.ndt) {
		this->rd = ve.rd;
	} else {
		this->rd.ndt = 0U;
	}
	if (ve.xd.ndt) {
		this->xd = ve.xd;
	} else {
		this->xd.ndt = 0U;
	}
	this->dur = echs_instant_diff(ve.e.till, ve.e.from);
	this->rdi = 0UL;
	this->ncch = 0UL;
	res = (echs_evstrm_t)this;
	if (ve.mr.nr && ve.mf.nu) {
		echs_evstrm_t aux;

		if (LIKELY((aux = get_aux_strm(ve.mf)) != NULL)) {
			res = make_evmrul(*ve.mr.r, res, aux);
		}
		/* otherwise display stream as is, maybe print a warning? */
	}
	/* better free this guy */
	if (nref <= 1U) {
		if (ve.mf.nu) {
			free(ve.mf.u);
		}
		if (ve.mr.nr) {
			free(ve.mr.r);
		}
	}
	return (echs_evstrm_t)res;
}

static echs_evstrm_t
make_evrrul(struct ical_vevent_s ve)
{
/* here's the deal, we check how many rrules there are, and
 * if it's just one we return a normal evrrul_s object, if
 * it's more than one we generate one evrrul_s object per
 * rrule and mux them together, sharing the resources in VE. */

	switch (ve.rr.nr) {
	case 0:
		return NULL;
	case 1:
		return __make_evrrul(ve, 1U);
	default:
		break;
	}
	with (echs_evstrm_t s[ve.rr.nr]) {
		struct ical_vevent_s ve_tmp = ve;
		size_t nr = 0UL;

		for (size_t i = 0U; i < ve.rr.nr; i++, nr++, ve_tmp.rr.r++) {
			s[nr] = __make_evrrul(ve_tmp, ve.rr.nr);
		}
		/* we've materialised these */
		free(ve.rr.r);
		if (ve.mr.nr) {
			free(ve.mr.r);
		}
		if (ve.mf.nu) {
			free(ve.mf.u);
		}
		return make_echs_evmux(s, nr);
	}
	return NULL;
}

static void
free_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	/* check reference counter */
	if (--this->nref) {
		return;
	}

	if (this->e.task) {
		free_echs_task(this->e.task);
	}
	if (this->xr.nr) {
		free(this->xr.r);
	}
	if (this->rd.ndt) {
		free(this->rd.dt);
	}
	if (this->xd.ndt) {
		free(this->xd.dt);
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evrrul(echs_const_evstrm_t s)
{
	const struct evrrul_s *this = (const struct evrrul_s*)s;
	struct evrrul_s *clon = malloc(sizeof(*this));

	*clon = *this;
	/* clone lists if applicable */
	if (this->e.task) {
		clon->e.task = echs_task_clone(this->e.task);
	}
	if (this->xr.nr) {
		clon->xr = clon_rrlst(this->xr);
	}
	if (this->rd.ndt) {
		clon->rd = clon_dtlst(this->rd);
	}
	if (this->xd.ndt) {
		clon->xd = clon_dtlst(this->xd);
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
	struct rrulsp_s *restrict rr = &strm->rr;

	assert(rr->freq > FREQ_NONE);
	if (UNLIKELY(!rr->count)) {
		return 0UL;
	}

	/* fill up with the proto instant */
	for (size_t j = 0U; j < countof(strm->cch); j++) {
		strm->cch[j] = strm->e.from;
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
		strm->e.from = strm->cch[strm->ncch];
	}
	if (UNLIKELY(strm->ncch == 0UL)) {
		return 0UL;
	}
	/* otherwise sort the array, just in case */
	echs_instant_sort(strm->cch, strm->ncch);

	/* also check if we need to rid some dates due to xdates */
	if (UNLIKELY(strm->xd.ndt > 0UL)) {
		const echs_instant_t *xd = strm->xd.dt;
		echs_instant_t *restrict rd = strm->cch;
		const echs_instant_t *const exd = xd + strm->xd.ndt;
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
	res = this->e;
	res.from = in;
	res.till = echs_instant_add(in, this->dur);
	return res;
nul:
	return nul;
}

static void
prnt_evrrul1(echs_const_evstrm_t s)
{
	const struct evrrul_s *this = (const struct evrrul_s*)s;

	prnt_ical_hdr();
	prnt_ev(this->e);

	fputs("RRULE:", stdout);
	prnt_rrul(&this->rr);

	prnt_ical_ftr();
	return;
}

static void
prnt_evrrulm(const echs_const_evstrm_t s[], size_t n)
{
/* we know that they all come from one ical_event_s originally,
 * print the proto event from the first guy, then all rrules in succession */
	const struct evrrul_s *this = (const void*)*s;
	size_t i;

	prnt_ical_hdr();
	prnt_ev(this->e);

	for (i = 0U; i < n; i++) {
		const struct evrrul_s *that = (const void*)s[i];

		if (!echs_event_eq_p(this->e, that->e)) {
			prnt_ical_ftr();
			goto one_by_one;
		}
		fputs("RRULE:", stdout);
		prnt_rrul(&that->rr);
	}
	prnt_ical_ftr();
	return;

one_by_one:
	for (; i < n; i++) {
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
			echs_evstrm_t tmp;
			echs_instant_t *xd;

			if (!a->ev[i].rr.nr && !a->ev[i].rd.ndt) {
				/* not an rrule but a normal vevent
				 * just him to the list */
				ev[nev++] = a->ev[i].e;
				/* free all the bits and bobs that
				 * might have been added */
				if (a->ev[i].xr.nr) {
					free(a->ev[i].xr.r);
				}
				if (a->ev[i].xd.ndt) {
					free(a->ev[i].xd.dt);
				}
				if (a->ev[i].mr.nr) {
					free(a->ev[i].mr.r);
				}
				if (a->ev[i].mf.nu) {
					free(a->ev[i].mf.u);
				}
				assert(a->ev[i].rr.r == NULL);
				continue;
			}
			/* it's an rrule, we won't check for
			 * exdates or exrules because they make
			 * no sense without an rrule to go with */
			/* check for exdates here, and sort them */
			if (UNLIKELY(a->ev[i].xd.ndt > 1UL &&
				     (xd = a->ev[i].xd.dt) != NULL)) {
				echs_instant_sort(xd, a->ev[i].xd.ndt);
			}

			if ((tmp = make_evrrul(a->ev[i])) != NULL) {
				s[ns++] = tmp;
			}
		}

		/* noone's using A anymore, so free it,
		 * we're reusing all the vevents in A so make sure
		 * we don't free them */
		free(a);
		a = NULL;

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


/* the push parser,
 * one day the file parser will be implemented in terms of this */
int
echs_evical_push(ical_parser_t p[static 1U], const char *buf, size_t bsz)
{
	struct ical_parser_s *_p = *p;

	if (UNLIKELY(_p == NULL)) {
		if (_ical_init_push(buf, bsz) < 0) {
			return -1;
		}

		/* otherwise we're on the right track,
		 * start a context now */
		if ((_p = *p = calloc(1U, sizeof(*_p))) == NULL) {
			return -1;
		}
	}
	_ical_push(_p, buf, bsz);
	return 0;
}

echs_evstrm_t
echs_evical_pull(ical_parser_t p[static 1U])
{
	struct ical_vevent_s *ve;
	echs_evstrm_t res = NULL;

	/* just let _ical_pull do the yakka and we split everything
	 * into evical vevents and evrruls */
	if (UNLIKELY(*p == NULL)) {
		/* how brave */
		;
	} else if ((ve = _ical_pull(*p)) == NULL) {
		/* we need more data, or we've reached the state finished */
		;
	} else if (UNLIKELY(ve == ICAL_EOP)) {
		/* oh, do the big cleaning up */
		_ical_fini(*p);
		free(*p);
		*p = NULL;
	} else if (!ve->rr.nr && !ve->rd.ndt) {
		/* not an rrule but a normal vevent */

		/* free all the bits and bobs that
		 * might have been added */
		if (ve->xr.nr) {
			free(ve->xr.r);
		}
		if (ve->xd.ndt) {
			free(ve->xd.dt);
		}
		if (ve->mr.nr) {
			free(ve->mr.r);
		}
		if (ve->mf.nu) {
			free(ve->mf.u);
		}
		assert(ve->rr.r == NULL);
		res = make_evical_vevent(&ve->e, 1U);
	} else {
		/* it's an rrule */
		echs_instant_t *xd;

		/* check for exdates here, and sort them */
		if (UNLIKELY(ve->xd.ndt > 1UL &&
			     (xd = ve->xd.dt) != NULL)) {
			echs_instant_sort(xd, ve->xd.ndt);
		}

		res = make_evrrul(*ve);
	}
	return res;
}

echs_evstrm_t
echs_evical_last_pull(ical_parser_t p[static 1U])
{
	echs_evstrm_t res = echs_evical_pull(p);

	if (LIKELY(*p != NULL)) {
		_ical_fini(*p);
		free(*p);
	}
	return res;
}

/* evical.c ends here */
