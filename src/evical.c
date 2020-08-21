/*** evical.c -- rfc5545/5546 to echs_task_t/echs_evstrm_t mapper
 *
 * Copyright (C) 2013-2020 Sebastian Freundt
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
#include "evfilt.h"
#include "nifty.h"
#include "evical-gp.c"
#include "evrrul-gp.c"
#include "evmrul-gp.c"
#include "evmeth-gp.c"
#include "evcomp-gp.c"
#include "fdprnt.h"
#include "echse-genuid.h"
#include "tzob.h"
#include "scale.h"
#include "shift.h"

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

/* this one can capture
 * - vtodo recurrences (orders)
 * - vtodo executions
 * - vtodo reports */
struct ical_vevent_s {
	echs_instant_t from;
	echs_instant_t till;
	echs_instant_t comp;
	echs_instant_t due;
	echs_idiff_t dur;

	echs_state_t sts;
	echs_scale_t cal;

	/* proto typical task */
	struct echs_task_s t;

	/* just to transport the method specified */
	ical_meth_t meth;
	/* request status or other status info */
	unsigned int req_status;

	/* pointers into the rrul/xrul array */
	struct rrlst_s rrul;
	struct rrlst_s xrul;
	/* points into the xdat/rdat array */
	struct dtlst_s rdat;
	struct dtlst_s xdat;
	/* points into the mrul array */
	struct mrlst_s mrul;
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


#define CHECK_RESIZE(o, id, iniz, nitems)				\
	if (UNLIKELY(!(o)->z##id)) {					\
		/* leave the first one out */				\
		void *tmp = malloc(iniz * sizeof(*(o)->id));		\
		if (UNLIKELY(tmp == NULL)) {				\
			return;						\
		}							\
		(o)->id = tmp;						\
		(o)->z##id = iniz;					\
		memset((o)->id, 0, sizeof(*(o)->id));			\
	}								\
	if (UNLIKELY((o)->n##id + nitems > (o)->z##id)) {		\
		do {							\
			const size_t nuz = (o)->z##id * 2U;		\
			void *tmp = realloc((o)->id, nuz * sizeof(*(o)->id)); \
			if (UNLIKELY(tmp == NULL)) {			\
				return;					\
			}						\
			(o)->id = tmp;					\
			(o)->z##id = nuz;				\
		} while ((o)->n##id + nitems > (o)->z##id);		\
	}

static void
add1_to_rrlst(struct rrlst_s *rl, const struct rrulsp_s *rr)
{
	CHECK_RESIZE(rl, r, 16U, 1U);
	rl->r[rl->nr++] = *rr;
	return;
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

static void
free_ical_vevent(struct ical_vevent_s *restrict ve)
{
	if (ve->rrul.nr) {
		free(ve->rrul.r);
	}
	if (ve->rdat.ndt) {
		free(ve->rdat.dt);
	}
	if (ve->xrul.nr) {
		free(ve->xrul.r);
	}
	if (ve->xdat.ndt) {
		free(ve->xdat.dt);
	}
	if (ve->mrul.nr) {
		free(ve->mrul.r);
	}
	return;
}

static inline echs_instant_t
echs_instant_to_utc(echs_instant_t i)
{
	echs_tzob_t z;

	if (UNLIKELY(z = echs_instant_tzob(i))) {
		i = echs_instant_utc(i, z);
	}
	return i;
}

static inline echs_event_t
echs_event_to_utc(echs_event_t e)
{
	e.from = echs_instant_to_utc(e.from);
	return e;
}

static inline echs_instant_t
echs_tzob_shift(echs_instant_t i, int off_from, int off_to)
{
	echs_idiff_t df = {.d = (off_to - off_from) * 1000};
	return echs_instant_add(i, df);
}


static echs_freq_t
snarf_freq(const char *spec)
{
	switch (*spec) {
	case 'Y':
		if (!strncmp(spec, "YEARLY", strlenof("YEARLY"))) {
			return FREQ_YEARLY;
		}
		break;
	case 'M':
		if (!strncmp(spec, "MINUTELY", strlenof("MINUTELY"))) {
			return FREQ_MINUTELY;
		} else if (!strncmp(spec, "MONTHLY", strlenof("MONTHLY"))) {
			return FREQ_MONTHLY;
		}
		break;
	case 'W':
		if (!strncmp(spec, "WEEKLY", strlenof("WEEKLY"))) {
			return FREQ_WEEKLY;
		}
		break;
	case 'D':
		if (!strncmp(spec, "DAILY", strlenof("DAILY"))) {
			return FREQ_DAILY;
		}
		break;
	case 'H':
		if (!strncmp(spec, "HOURLY", strlenof("HOURLY"))) {
			return FREQ_HOURLY;
		}
		break;
	case 'S':
		if (!strncmp(spec, "SECONDLY", strlenof("SECONDLY"))) {
			return FREQ_SECONDLY;
		}
		break;
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

static echs_scale_t
snarf_scale(const char *spec)
{
	echs_scale_t r = SCALE_GREGORIAN;

	switch (*spec) {
	default:
	case 'G':
		break;
	case 'H':
		r = SCALE_HIJRI_UMMULQURA;
		if (UNLIKELY(spec[5U] == '.')) {
			/* one of the Hijris */
			switch (spec[6U]) {
			default:
			case 'U'/*MMULQURA*/:
				break;
			case 'D'/*IYANET*/:
				r = SCALE_HIJRI_DIYANET;
				break;
			case 'I': {
				/* Gent's types */
				const char *kp = spec + 7U;
				r = SCALE_HIJRI_IA;
				r += (echs_scale_t)((*kp == 'V' || *kp++ == 'I') * 2U);
				r += (echs_scale_t)((*kp == 'V' || *kp++ == 'I') * 2U);
				r += (echs_scale_t)((*kp == 'C'));
				r += (echs_scale_t)((*kp == 'V') ? 2U : 0U);
				r += (echs_scale_t)(*++kp == 'C');
				break;
			}
			}
		}
		break;
	}
	return r;
}

static echs_shift_t
snarf_shift(const char *spec)
{
	echs_shift_t r = 0U;
	char *on = NULL;

	switch (*spec) {
	case '-':
		r |= 0b1U;
		/*@fallthrough@*/
	case '+':
		spec++;
	default:
		break;
	}
	with (long int tmp = strtol(spec, &on, 10)) {
		if (UNLIKELY(on == NULL || tmp < 0 || tmp > 366)) {
			return 0;
		}
		r |= tmp << 3U;
		spec = on;
	}
	switch (*spec) {
	case 'b':
	case 'B':
		r |= 0b10U;
		if (UNLIKELY(r >> 3U > 260U)) {
			return 0;
		}
		/* check shift semantics, + means go to Monday, - means go to Friday */
		switch (*++spec) {
		case '\0':
		case ';':
			break;
		case '+':
			r |= !(r & 0b1U) << 2U;
			break;
		case '-':
			r |= (r & 0b1U) << 2U;
			break;
		default:
			return 0;
		}
		break;
	case '\0':
	case ';':
		break;
	default:
		return 0;
	}
	/* only allow -0B, not -0 */
	r -= r == 1U;
	return r;
}

static struct rrulsp_s
snarf_rrule(const char *s, size_t z)
{
	struct rrulsp_s rr = {
		.freq = FREQ_NONE,
		.count = -1,
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
			/* hmm? this won't be no use to us, next */
			continue;
		} else if (kv > eofld) {
			/* still no use */
			continue;
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
				rr.count = tmp;
				break;
			case KEY_INTER:
				rr.inter = (unsigned int)tmp;
				break;
			}
			break;

		case KEY_UNTIL:
			rr.until = dt_strp(++kv, NULL, 0U);
			break;

		case KEY_SCALE:
			rr.scale = snarf_scale(++kv);
			break;

		case KEY_SHIFT:
			rr.shift = snarf_shift(++kv);
			break;

		case BY_WDAY:
			/* this one's special in that weekday names
			 * are allowed to follow the indicator number */
			on = NULL;
			do {
				echs_wday_t w;

				tmp = strtol(++kv, &on, 10);
				if (UNLIKELY(on == NULL)) {
					;
				} else if ((w = snarf_wday(on)) == MIR) {
					;
				} else if (tmp >= -53 && tmp <= 53) {
					/* otherwise assign */
					ass_bi447(&rr.dow, pack_cd(CD(tmp, w)));
				}
			} while (on && (kv = strchr(on, ',')) != NULL);
			break;
		case BY_MON:
		case BY_HOUR:
		case BY_MIN:
		case BY_SEC:
			do {
				long unsigned int tmu;

				on = NULL;
				/* tmp == 0U is explicitly allowed sometimes
				 * so do the naught check where it matters */
				tmu = strtoul(++kv, &on, 10);

				switch (c->key) {
				case BY_MON:
					if (LIKELY(tmu && tmu <= 12U)) {
						rr.mon = ass_bui31(rr.mon, tmu);
					}
					break;
				case BY_HOUR:
					if (LIKELY(tmu <= 24U)) {
						rr.H = ass_bui31(rr.H, tmu);
					}
					break;
				case BY_MIN:
					if (LIKELY(tmu < 60U)) {
						rr.M = ass_bui63(rr.M, tmu);
					}
					break;
				case BY_SEC:
					if (LIKELY(tmu <= 60U)) {
						rr.S = ass_bui63(rr.S, tmu);
					}
					break;
				}
			} while (on && *(kv = on) == ',');
			break;

		case BY_MDAY:
		case BY_WEEK:
		case BY_YDAY:
		case BY_POS:
			/* these ones take +/- values */
			do {
				on = NULL;
				if (UNLIKELY(!(tmp = strtol(++kv, &on, 10)))) {
					/* we never allow 0 */
					continue;
				}
				switch (c->key) {
				case BY_MDAY:
					if (LIKELY(tmp < 32 && tmp > -32)) {
						rr.dom = ass_bi31(rr.dom, tmp);
					}
					break;
				case BY_WEEK:
					if (LIKELY(tmp <= 53 && tmp >= -53)) {
						rr.wk = ass_bi63(rr.wk, tmp);
					}
					break;
				case BY_YDAY:
					if (LIKELY(tmp <= 366 && tmp >= -366)) {
						ass_bi383(&rr.doy, tmp);
					}
					break;
				case BY_POS:
					if (LIKELY(tmp <= 366 && tmp >= -366)) {
						ass_bi383(&rr.pos, tmp);
					}
					break;
				}
			} while (on && *(kv = on) == ',');
			break;

			/* extensions */
		case BY_EASTER:
			/* these ones take +/- values */
			do {
				on = NULL;
				tmp = strtol(++kv, &on, 10);
				if (LIKELY(tmp <= 366 && tmp >= -366)) {
					ass_bi383(&rr.easter, tmp);
				}
			} while (on && *(kv = on) == ',');
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
			/* hmm? this won't be no use to us, next */
			continue;
		} else if (kv > eofld) {
			/* still no use */
			continue;
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

static echs_instant_t
snarf_dt(const char *eof, const char *vp, const char *const ep)
{
	char *on = NULL;
	echs_instant_t res = dt_strp(vp, &on, ep - vp);

	if (UNLIKELY(on == NULL)) {
		return res;
	}
	while (*eof++ == ';') {
		/* we've got a field modifier */
		static const char tzid[] = "TZID=";
		static const char scal[] = "SCALE=";
		/* next EOF */
		const char *const neo = strpbrk(eof, ":;") ?: vp - 1U;

		if (0) {
			;
		} else if (!strncmp(eof, tzid, strlenof(tzid))) {
			/* yep, got him */
			if (on < ep && *on == 'Z') {
				/* don't worry about the zone spec */
				;
			} else {
				const char *const zn = eof + strlenof(tzid);
				const size_t nzn = neo - zn;
				echs_tzob_t z = echs_tzob(zn, nzn);

				res = echs_instant_attach_tzob(res, z);
			}
		} else if (!strncmp(eof, scal, strlenof(scal))) {
			/* very nice */
			const char *const zn = eof + strlenof(scal);
			echs_scale_t s = snarf_scale(zn);

			res = echs_instant_attach_scale(res, s);
		}

		/* set eof for next round */
		eof = neo;
	}
	return res;
}

static struct dtlst_s
snarf_dtlst(const char *eof, const char *vp, const char *const ep)
{
	struct dtlst_s dl = {NULL};
	echs_tzob_t z = 0U;
	echs_scale_t s = SCALE_GREGORIAN;

	while (*eof++ == ';') {
		/* we've got a field modifier, the only modifier
		 * we can do with (atm) is TZID so try and read that */
		static const char tzid[] = "TZID=";
		static const char scal[] = "SCALE=";
		const char *const neo = strpbrk(eof, ":;") ?: vp - 1U;

		if (0) {
			;
		} else if (!strncmp(eof, tzid, strlenof(tzid))) {
			/* yep, got him */
			const char *const zn = eof + strlenof(tzid);
			const size_t nzn = neo - zn;

			z = echs_tzob(zn, nzn);
		} else if (!strncmp(eof, scal, strlenof(scal))) {
			/* very nice */
			const char *const zn = eof + strlenof(scal);
			s = snarf_scale(zn);
		}

		/* get ready for the next round */
		eof = neo;
	}
	for (const char *eod; vp < ep; vp = eod + 1U) {
		echs_instant_t in;
		char *on = NULL;

		if (UNLIKELY((eod = strchr(vp, ',')) == NULL)) {
			eod = ep;
		}
		in = dt_strp(vp, &on, eod - vp);
		if (UNLIKELY(echs_instant_0_p(in) || on == NULL)) {
			continue;
		}
		/* attach zone (if any) and only if there's no zone indicator */
		if (on >= eod || *on != 'Z') {
			in = echs_instant_attach_tzob(in, z);
		}
		/* attach scale */
		in = echs_instant_attach_scale(in, s);
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
snarf_fld(struct ical_vevent_s ve[static 1U],
	  ical_fld_t fld, const char *eof, const char *vp, const char *const ep)
{
/* vevent field parser */
	if (UNLIKELY(!(ep - vp))) {
		/* don't want empty values */
		return -1;
	}

	switch (fld) {
		char *on;

	default:
	case FLD_UNK:
		/* how did we get here */
		return -1;
	case FLD_DTSTART:
	case FLD_DTEND:
	case FLD_DUE:
	case FLD_COMPL:
		with (echs_instant_t i = snarf_dt(eof, vp, ep)) {
			switch (fld) {
			case FLD_DTSTART:
				ve->from = i;
				break;
			case FLD_COMPL:
				ve->comp = i;
				break;
			case FLD_DTEND:
				ve->till = i;
				break;
			case FLD_DUE:
				ve->due = i;
				break;
			}
		}
		break;
	case FLD_DURA:
		with (echs_idiff_t i = idiff_strp(vp, &on, ep - vp)) {
			if (on >= ep) {
				/* keep track of it and do the maths later */
				ve->dur = i;
			}
		}
		break;

	case FLD_XDATE:
	case FLD_RDATE:
		/* otherwise snarf */
		with (struct dtlst_s l = snarf_dtlst(eof, vp, ep)) {
			if (l.ndt == 0UL) {
				break;
			}
			switch (fld) {
			case FLD_XDATE:
				ve->xdat = l;
				break;
			case FLD_RDATE:
				ve->rdat = l;
				break;
			}
		}
		break;
	case FLD_RRULE:
	case FLD_XRULE:
		/* otherwise snarf him */
		if_with (struct rrulsp_s r,
			 (r = snarf_rrule(vp, ep - vp)).freq != FREQ_NONE) {
			switch (fld) {
			case FLD_RRULE:
				/* bang to global array */
				add1_to_rrlst(&ve->rrul, &r);
				break;
			case FLD_XRULE:
				/* bang to global array */
				add1_to_rrlst(&ve->xrul, &r);
				break;
			}
		}
		break;
	case FLD_MRULE:
		/* otherwise snarf him */
		if_with (struct mrulsp_s r,
			 (r = snarf_mrule(vp, ep - vp)).mdir != MDIR_NONE) {
			/* bang to global array */
			add1_to_mrlst(&ve->mrul, r);
		}
		break;
	case FLD_STATE:
		for (const char *eos; vp < ep; vp = eos + 1U) {
			echs_state_t st;

			eos = strchr(vp, ',') ?: ep;
			if (!(st = add_state(vp, eos - vp))) {
				continue;
			}
			ve->sts = stset_add_state(ve->sts, st);
		}
		break;
	case FLD_MFILE:
		/* aah, an event-wide MFILE directive,
		 * too bad we had to turn these off (b480f83 still has them) */
		break;
	case FLD_UID:
		ve->t.oid = intern(vp, ep - vp);
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
		if (ve->t.desc != NULL) {
			/* only the first description wins */
			break;
		}
		if (vp < ep) {
			ve->t.desc = strndup(vp, ep - vp);
		}
		break;

	case FLD_LOC:
		if (ve->t.run_as.wd != NULL) {
			/* only the first location wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.run_as.wd = strndup(vp, ep - vp);
		break;

	case FLD_IFILE:
		if (ve->t.in != NULL) {
			/* only the first location wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.in = strndup(vp, ep - vp);
		break;

	case FLD_OFILE:
		if (ve->t.out != NULL) {
			/* only the first location wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.out = strndup(vp, ep - vp);
		break;

	case FLD_EFILE:
		if (ve->t.err != NULL) {
			/* only the first location wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.err = strndup(vp, ep - vp);
		break;

	case FLD_MOUT:
		if (vp < ep) {
			switch (*vp) {
			case '0':
			case 'f':
			case 'F':
				ve->t.mailout = 0U;
				ve->t.moutset = 1U;
				break;
			default:
				ve->t.mailout = 1U;
				ve->t.moutset = 1U;
				break;
			}
		}
		break;

	case FLD_MERR:
		if (vp < ep) {
			switch (*vp) {
			case '0':
			case 'f':
			case 'F':
				ve->t.mailerr = 0U;
				ve->t.merrset = 1U;
				break;
			default:
				ve->t.mailerr = 1U;
				ve->t.merrset = 1U;
				break;
			}
		}
		break;

	case FLD_MRUN:
		if (vp < ep) {
			switch (*vp) {
			case '0':
			case 'f':
			case 'F':
				ve->t.mailrun = 0U;
				ve->t.mrunset = 1U;
				break;
			default:
				ve->t.mailrun = 1U;
				ve->t.mrunset = 1U;
				break;
			}
		}
		break;

	case FLD_SHELL:
		if (ve->t.run_as.sh != NULL) {
			/* only the first shell wins */
			break;
		}
		/* bang straight into the proto task */
		ve->t.run_as.sh = strndup(vp, ep - vp);
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

	case FLD_MAX_SIMUL:
		with (long int i = strtol(vp, &on, 0)) {
			if (UNLIKELY(on < ep)) {
				/* couldn't read it */
				;
			} else if (UNLIKELY(i < 0 || i >= 077)) {
				/* can't use that value, can we? */
				;
			} else {
				/* off-by-one assignment */
				ve->t.max_simul = i + 1;
			}
		}
		break;

	case FLD_UMASK:
		with (long int i = strtol(vp, &on, 8)) {
			if (UNLIKELY(on < ep)) {
				/* couldn't read it */
				;
			} else if (UNLIKELY(i < 0 || i > 0777)) {
				/* not a umask we want */
				;
			} else {
				/* off by one assignment */
				ve->t.umsk = i + 1U;
			}
		}
		break;

	case FLD_OWNER:
	case FLD_SUID:
	case FLD_SGID:
		with (long int i = strtol(vp, &on, 0)) {
			if (UNLIKELY(on < ep)) {
				/* nope */
				const char *s = strndup(vp, ep - vp);

				switch (fld) {
				case FLD_OWNER:
					ve->t.owner = nummapstr_bang_str(s);
					break;
				case FLD_SUID:
					ve->t.run_as.u = nummapstr_bang_str(s);
					break;
				case FLD_SGID:
					ve->t.run_as.g = nummapstr_bang_str(s);
					break;
				}
				break;
			}
			/* numerical assignments here */
			switch (fld) {
			case FLD_OWNER:
				ve->t.owner = nummapstr_bang_num(i);
				break;
			case FLD_SUID:
				ve->t.run_as.u = nummapstr_bang_num(i);
				break;
			case FLD_SGID:
				ve->t.run_as.g = nummapstr_bang_num(i);
				break;
			}
		}
		break;

	case FLD_RSTAT:
		/* aaah we're reading a response (reply) */
		if (vp < ep) {
			ve->req_status = *vp ^ '0';
		}
		break;

	case FLD_RECURID:
		ve->from = dt_strp(vp, NULL, 0U);
		if (ep[-1] == '+') {
			/* oh, they want to cancel all from then on */
			ve->till = echs_max_instant();
		} else {
			/* they're just cancelling this one instance */
			ve->till = echs_nul_instant();
		}
		break;
	}
	return 0;
}

static int
snarf_pro(struct ical_vevent_s ve[static 1U],
	  ical_fld_t fld, const char *eof, const char *vp, const char *const ep)
{
/* prologue snarfer, should only be called whilst being in VCAL state */
	(void)eof;

	if (UNLIKELY(!(ep - vp))) {
		/* don't want empty values */
		return -1;
	}

	/* inspect the field */
	switch (fld) {
		char *on;

	case FLD_MFILE:
		/* aah, a file-wide MFILE directive,
		 * too bad we had to turn these off (b480f83 still has them) */
		break;

	case FLD_METH:;
		/* oooh, they've been so kind as to give us precise
		 * instructions ... */
		const struct ical_meth_cell_s *m;

		if ((m = __evical_meth(vp, ep - vp)) == NULL) {
			/* nope, no methods given */
			break;
		}
		ve->meth = m->meth;
		break;

	case FLD_MAX_SIMUL:
		with (long int i = strtol(vp, &on, 0)) {
			if (UNLIKELY(on < ep)) {
				/* couldn't read it */
				;
			} else if (UNLIKELY(i < 0 || i >= 077)) {
				/* can't use that value, can we? */
				;
			} else {
				/* off-by-one assignment */
				ve->t.max_simul = i + 1;
			}
		}
		break;

	case FLD_OWNER:
	case FLD_UMASK:
	case FLD_SUID:
	case FLD_SGID:
		snarf_fld(ve, fld, eof, vp, ep);
		break;

	case FLD_SCALE:
		ve->cal = snarf_scale(vp);
		break;

	default:
		break;
	}
	return 0;
}


/* ical parsers, push and pull */
struct ical_parser_s {
	enum {
		ST_UNK,
		ST_VCAL,
		ST_VTOD,
		ST_VOTH,
	} st;
	struct ical_vevent_s ve;
	struct ical_vevent_s globve;

	echs_instruc_t protoi;

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
	if (UNLIKELY(buf == NULL || bsz == 0U)) {
		/* huh? no buffer AND no context? */
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
	const char *const ep = sp + sz;
	struct ical_vevent_s *res = NULL;
	const char *eofld;
	const char *vp;
	const struct ical_fld_cell_s *c;

	if (UNLIKELY((eofld = strpbrk(sp, ":;")) == NULL)) {
		goto out;
	} else if (UNLIKELY((c = __evical_fld(sp, eofld - sp)) == NULL)) {
		goto out;
	}

	/* obtain the value pointer */
	if (LIKELY(*(vp = eofld) == ':' || (vp = strchr(eofld, ':')) != NULL)) {
		vp++;
	} else {
		goto out;
	}

	switch (p->st) {
	case ST_UNK:
		/* we're before or behind a VCALENDAR,
		 * fingers crossed we find one and can finally start parsing */
		switch (c->fld) {
			const struct ical_comp_cell_s *comp;

		default:
			/* all bullshit */
			res = ICAL_EOP;
			break;

		case FLD_BEGIN:
			/* oh oh oh, looks promising innit? */
			if ((comp = __evical_comp(vp, ep - vp)) == NULL ||
			    comp->comp != COMP_VCAL) {
				p->st = ST_VOTH;
			} else {
				/* rinse our bucket */
				memset(&p->globve, 0, sizeof(p->globve));
				/* switch state */
				p->st = ST_VCAL;
			}
			break;
		}
		break;

	default:
	case ST_VOTH:
		switch (c->fld) {
			const struct ical_comp_cell_s *comp;

		default:
			/* whatever */
			break;
		case FLD_BEGIN:
			/* just up the depth tracker */
			p->st++;
			break;

		case FLD_END:
			if ((comp = __evical_comp(vp, ep - vp)) == NULL ||
			    comp->comp != COMP_VCAL) {
				/* just down the depth tracker */
				if (p->st-- == ST_VOTH) {
					/* and fall back to VCAL */
					p->st = ST_VCAL;
				}
			} else {
				/* oooh END:VCALENDAR?
				 * well we better fuck off then */
				p->st = ST_UNK;
				res = ICAL_EOP;
			}
			break;
		}
		break;

	case ST_VCAL:
		/* check for globals */
		switch (c->fld) {
			const struct ical_comp_cell_s *comp;

		default:
			/* we're in a vcalendar component, let the prologue
			 * snarfer figure out what we want */
			snarf_pro(&p->globve, c->fld, eofld, vp, ep);
			break;

		case FLD_BEGIN:
		case FLD_END:
			/* oh oh oh, looks promising innit? */
			if ((comp = __evical_comp(vp, ep - vp)) == NULL) {
				if (UNLIKELY(c->fld == FLD_END)) {
					/* what's ending here? */
					p->st = ST_UNK;
					res = ICAL_EOP;
					break;
				} else if (c->fld == FLD_BEGIN) {
					/* ah something else is starting */
					p->st = ST_VOTH;
					break;
				}
				/* not reached */
			}
			switch (comp->comp) {
			case COMP_VTOD:
			case COMP_VEVT:
				if (LIKELY(c->fld == FLD_BEGIN)) {
					/* FINALLY a vevent thing */
					/* rinse our bucket */
					memset(&p->ve, 0, sizeof(p->ve));
					/* copy global task properties */
					p->ve.t = p->globve.t;
					/* copy global scale */
					p->ve.cal = p->globve.cal;
					/* and set state to vevent */
					p->st = ST_VTOD;
					break;
				}
				/* otherwise we've ended a VEVENT whilst
				 * being in a VCALENDAR container, that's
				 * just plain wrong */
				p->st = ST_UNK;
				res = ICAL_EOP;
				break;

			case COMP_VCAL:
				if (LIKELY(c->fld == FLD_END)) {
					/* yay, we're finished */
					p->st = ST_UNK;
					res = ICAL_EOP;
					break;
				}
				/*@fallthrough@*/
			default:
				/* vcal in vcal? or, worse, some other field? */
				p->st = ST_UNK;
				res = ICAL_EOP;
				break;
			}
			break;
		}
		break;

	case ST_VTOD:
		/* check for globals */
		switch (c->fld) {
			const struct ical_comp_cell_s *comp;

		default:
			/* pass on field parsing to helper routines */
			if (snarf_fld(&p->ve, c->fld, eofld, vp, ep) < 0) {
				/* no state change */
				;
			}
			break;

		case FLD_BEGIN:
			/* that's utterly bogus, nothing can begin inside
			 * a VEVENT component */
			p->st = ST_UNK;
			res = ICAL_EOP;
			break;

		case FLD_END:
			/* yep, that might be legit */
			if ((comp = __evical_comp(vp, ep - vp)) == NULL ||
			    !(comp->comp == COMP_VEVT ||
			      comp->comp == COMP_VTOD)) {
				/* nope it wasn't legit */
				p->st = ST_UNK;
				res = ICAL_EOP;
				break;
			}
			/* return to VCAL state so we can be looking forward
			 * to other vevents as well */
			if (!p->ve.t.owner) {
				/* bang owner */
				p->ve.t.owner = p->globve.t.owner;
			}
			if (!p->ve.t.umsk) {
				/* bang umask */
				p->ve.t.umsk = p->globve.t.umsk;
			}
			if (!p->ve.t.max_simul) {
				/* bang umask */
				p->ve.t.max_simul = p->globve.t.max_simul;
			}
			if (!p->ve.t.run_as.u) {
				/* bang run_as */
				p->ve.t.run_as = p->globve.t.run_as;
			}
			/* copy global scale */
			p->ve.cal = p->globve.cal;
			/* reset to unknown state */
			p->st = ST_VCAL;
			res = &p->ve;
			/* success */
			break;
		}
		break;
	}
out:
	/* we've consumed him */
	p->six = 0U;
	return res;
}

static struct ical_vevent_s*
_ical_pull(struct ical_parser_s p[static 1U])
{
/* pull-version of read_ical */
	struct ical_vevent_s *res = NULL;
	const char *eol;

#define BP	(p->buf + p->bix)
#define BZ	(p->bsz - p->bix)
#define BI	(p->bix)
	/* before delving into the current buffer check the stash,
	 * we might have put a multiline there and only now it
	 * becomes apparent that it's indeed a valid line when
	 * examinging the new bytes in the parser buffer */
	if (p->six && p->stash[p->six] == '\001') {
		/* go back to 0 termination */
		p->stash[p->six] = '\0';
		/* now check if the stuff in the buffer happens
		 * to start with a single allowed whitespace in
		 * which case we enter the normal chop_more
		 * procedure */
		if (LIKELY(*BP != ' ' && *BP != '\t')) {
			goto proc;
		}
		/* just get on with it */
	}
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
		if (eol != NULL) {
			/* means at least we've seen a \n up there
			 * leave a mark in the stash buffer so the
			 * pre-examination in the next iteration can
			 * rule whether this was a multi-line or in
			 * fact a complete line */
			p->stash[p->six] = '\001';
		}
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

	proc:
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

	if (UNLIKELY(p->st == ST_VTOD)) {
		free_ical_vevent(&p->ve);
	}
	/* free the globve */
	free_ical_vevent(&p->globve);
	/* dissolve all of it */
	memset(p, 0, sizeof(*p));
	return;
}


/* sending is like printing but into a file descriptor of choice */
static void
send_task(int whither, echs_task_t t)
{
	static unsigned int auto_uid;

	/* fill in a missing uid? */
	auto_uid++;

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);

	if (t->oid) {
		fdprintf("UID:%s\n", obint_name(t->oid));
	} else {
		/* it's mandatory, so generate one */
		fdprintf("UID:echse_merged_vevent_%u\n", auto_uid);
	}
	switch (t->vtod_typ) {
	default:
	case VTOD_TYP_UNK:
		/* nothing to print */
		break;
	case VTOD_TYP_TIMEOUT: {
		char stmp[32U] = "TIMEOUT:";
		size_t n = strlenof("TIMEOUT:");

		n += idiff_strf(stmp + n, sizeof(stmp) - n, t->timeout);
		stmp[n++] = '\n';
		fdwrite(stmp, n);
		break;
	}
	case VTOD_TYP_DUE: {
		char stmp[32U] = "DUE:";
		size_t n = strlenof("DUE:");

		n += dt_strf_ical(stmp + n, sizeof(stmp) - n, t->due);
		stmp[n++] = '\n';
		fdwrite(stmp, n);
		break;
	}
	case VTOD_TYP_COMPL: {
		char stmp[32U] = "COMPLETED:";
		size_t n = strlenof("COMPLETED:");

		n += dt_strf_ical(stmp + n, sizeof(stmp) - n, t->compl);
		stmp[n++] = '\n';
		fdwrite(stmp, n);
		break;
	}
	}
	if (t->cmd) {
		fdprintf("SUMMARY:%s\n", t->cmd);
	}
	if (t->desc) {
		fdprintf("DESCRIPTION:%s\n", t->desc);
	}
	if (t->org) {
		fdprintf("ORGANIZER:%s\n", t->org);
	}
	if (t->att) {
		for (const char *const *ap = t->att->l; *ap; ap++) {
			fdprintf("ATTENDEE:%s\n", *ap);
		}
	}
	if (t->in) {
		fdprintf("X-ECHS-IFILE:%s\n", t->in);
	}
	if (t->out) {
		fdprintf("X-ECHS-OFILE:%s\n", t->out);
	}
	if (t->err) {
		fdprintf("X-ECHS-EFILE:%s\n", t->err);
	}
	with (nummapstr_t u = t->run_as.u) {
		const char *tmps;
		uintptr_t tmpn;

		if ((tmps = nummapstr_str(u))) {
			fdprintf("X-ECHS-SETUID:%s\n", tmps);
		} else if ((tmpn = nummapstr_num(u)) != NUMMAPSTR_NAN) {
			fdprintf("X-ECHS-SETUID:%u\n", (unsigned int)tmpn);
		}
	}
	with (nummapstr_t g = t->run_as.g) {
		const char *tmps;
		uintptr_t tmpn;

		if ((tmps = nummapstr_str(g))) {
			fdprintf("X-ECHS-SETGID:%s\n", tmps);
		} else if ((tmpn = nummapstr_num(g)) != NUMMAPSTR_NAN) {
			fdprintf("X-ECHS-SETGID:%u\n", (unsigned int)tmpn);
		}
	}
	if (t->run_as.sh) {
		fdprintf("X-ECHS-SHELL:%s\n", t->run_as.sh);
	}
	if (t->run_as.wd) {
		fdprintf("LOCATION:%s\n", t->run_as.wd);
	}
	if (t->umsk <= 0777) {
		fdprintf("X-ECHS-UMASK:0%o\n", t->umsk);
	}
	if (t->mrunset) {
		fdprintf("X-ECHS-MAIL-RUN:%u\n", (unsigned int)t->mailrun);
	}
	if (t->moutset) {
		fdprintf("X-ECHS-MAIL-OUT:%u\n", (unsigned int)t->mailout);
	}
	if (t->merrset) {
		fdprintf("X-ECHS-MAIL-ERR:%u\n", (unsigned int)t->mailerr);
	}
	if (t->max_simul < 077U) {
		fdprintf("X-ECHS-MAX-SIMUL:%d\n", t->max_simul);
	}
	return;
}

static void
send_ical_hdr(int whither, bool vtodop)
{
	static const char bege[] = "BEGIN:VEVENT\n";
	static const char begt[] = "BEGIN:VTODO\n";
	static char stmp[32U] = "DTSTAMP:";
	static size_t ztmp = strlenof("DTSTAMP:");
	/* singleton, there's only one now */
	static time_t now;
	const char *beg = bege;
	size_t nbeg = strlenof(bege);

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);

	if (vtodop) {
		beg = begt;
		nbeg = strlenof(begt);
	}
	fdwrite(beg, nbeg);
	if (UNLIKELY(now <= 0)) {
		echs_instant_t nowi;

		if (UNLIKELY(time(&now) <= 0)) {
			/* screw up the singleton */
			now = 0;
			return;
		}
		/* otherwise fill in now and materialise */
		nowi = epoch_to_echs_instant(time(&now));
		ztmp += dt_strf_ical(stmp + ztmp, sizeof(stmp) - ztmp, nowi);
		stmp[ztmp++] = '\n';
	}
	fdwrite(stmp, ztmp);
	return;
}

static void
send_ical_ftr(int whither, bool vtodp)
{
	static const char ende[] = "END:VEVENT\n";
	static const char endt[] = "END:VTODO\n";
	const char *end = ende;
	size_t nend = strlenof(ende);

	if (vtodp) {
		end = endt;
		nend = strlenof(endt);
	}

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);
	fdwrite(end, nend);
	/* that's the last thing in line, just send it off */
	fdflush();
	return;
}

static void
send_stset(int whither, echs_stset_t sts)
{
	echs_state_t st = 0U;

	if (UNLIKELY(!sts)) {
		return;
	}

	fdbang(whither);
	for (; sts && !(sts & 0b1U); sts >>= 1U, st++);
	with (const char *sn = state_name(st)) {
		size_t sz = strlen(sn);
		fdwrite(sn, sz);
	}
	/* print list of states,
	 * we should probably use an iter from state.h here */
	while (st++, sts >>= 1U) {
		const char *sn;
		for (; sts && !(sts & 0b1U); sts >>= 1U, st++);

		if (LIKELY((sn = state_name(st)) != NULL)) {
			size_t sz = strlen(sn);
			fdputc(',');
			fdwrite(sn, sz);
		}
	}
	return;
}

static void
send_scale(echs_scale_t sca)
{
	switch (sca) {
	default:
	case SCALE_GREGORIAN:
		break;
	case SCALE_HIJRI_IA:
		fdwrite(";SCALE=HIJRI.IA", strlenof(";SCALE=HIJRI.IA"));
		break;
	case SCALE_HIJRI_IC:
		fdwrite(";SCALE=HIJRI.IC", strlenof(";SCALE=HIJRI.IC"));
		break;
	case SCALE_HIJRI_IIA:
		fdwrite(";SCALE=HIJRI.IIA", strlenof(";SCALE=HIJRI.IIA"));
		break;
	case SCALE_HIJRI_IIC:
		fdwrite(";SCALE=HIJRI.IIC", strlenof(";SCALE=HIJRI.IIC"));
		break;
	case SCALE_HIJRI_IIIA:
		fdwrite(";SCALE=HIJRI.IIIA", strlenof(";SCALE=HIJRI.IIIA"));
		break;
	case SCALE_HIJRI_IIIC:
		fdwrite(";SCALE=HIJRI.IIIC", strlenof(";SCALE=HIJRI.IIIC"));
		break;
	case SCALE_HIJRI_IVA:
		fdwrite(";SCALE=HIJRI.IVA", strlenof(";SCALE=HIJRI.IVA"));
		break;
	case SCALE_HIJRI_IVC:
		fdwrite(";SCALE=HIJRI.IVC", strlenof(";SCALE=HIJRI.IVC"));
		break;
	case SCALE_HIJRI_UMMULQURA:
		fdwrite(";SCALE=HIJRI.UMMULQURA", strlenof(";SCALE=HIJRI.UMMULQURA"));
		break;
	case SCALE_HIJRI_DIYANET:
		fdwrite(";SCALE=HIJRI.DIYANET", strlenof(";SCALE=HIJRI.DIYANET"));
		break;
	}
	return;
}

static void
send_ev(int whither, echs_event_t e, echs_tzob_t z)
{
	char stmp[32U] = {':'};
	size_t ztmp = 1U;
	echs_scale_t sca;
	const char *zn;

	if (UNLIKELY(echs_nul_instant_p(e.from))) {
		return;
	}

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);

	/* never wrong with this one */
	fdwrite("DTSTART", strlenof("DTSTART"));
	if (echs_instant_all_day_p(e.from)) {
		fdwrite(";VALUE=DATE", strlenof(";VALUE=DATE"));
	}
	if ((sca = echs_instant_scale(e.from))) {
		send_scale(sca);
		/* scale can bog off now */
		e.from = echs_instant_detach_scale(e.from);
	}

	if (z && (zn = echs_zone(z))) {
		size_t zz = strlen(zn);

		fdwrite(";TZID=", strlenof(";TZID="));
		fdwrite(zn, zz);

		/* also convert e.from to local Z-time */
		e.from = echs_instant_loc(e.from, z);
	}
	/* the actual stamp */
	ztmp = dt_strf_ical(stmp + 1U, sizeof(stmp) - 1U, e.from);
	stmp[ztmp++ + 1U] = '\n';
	fdwrite(stmp, ztmp + 1U);

	ztmp = idiff_strf(stmp + 1U, sizeof(stmp) - 1U, e.dur);
	stmp[++ztmp] = '\n';
	fdwrite("DURATION", strlenof("DURATION"));
	fdwrite(stmp, ztmp + 1U);
	if (e.sts) {
		fdwrite("X-GA-STATE:", strlenof("X-GA-STATE:"));
		send_stset(whither, e.sts);
		fdputc('\n');
	}
	return;
}

static void
send_cd(int whither, struct cd_s cd)
{
	static const char *w[] = {
		"MI", "MO", "TU", "WE", "TH", "FR", "SA", "SU"
	};

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);

	if (cd.cnt) {
		fdprintf("%d", cd.cnt);
	}
	fdwrite(w[cd.dow], 2U);
	return;
}

static void
send_rrul(int whither, rrulsp_t rr, size_t ccnt)
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

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);

	fdprintf("RRULE:%s", f[rr->freq]);

	if (rr->inter > 1U) {
		fdprintf(";INTERVAL=%u", rr->inter);
	}
	if (rr->scale) {
		send_scale(rr->scale);
	}
	with (unsigned int m) {
		bitint_iter_t i = 0UL;

		if (!bui31_has_bits_p(rr->mon)) {
			break;
		}
		m = bui31_next(&i, rr->mon);
		fdprintf(";BYMONTH=%u", m);
		while (m = bui31_next(&i, rr->mon), i) {
			fdprintf(",%u", m);
		}
	}

	with (int yw) {
		bitint_iter_t i = 0UL;

		if (!bi63_has_bits_p(rr->wk)) {
			break;
		}
		yw = bi63_next(&i, rr->wk);
		fdprintf(";BYWEEKNO=%d", yw);
		while (yw = bi63_next(&i, rr->wk), i) {
			fdprintf(",%d", yw);
		}
	}

	with (int yd) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->doy)) {
			break;
		}
		yd = bi383_next(&i, &rr->doy);
		fdprintf(";BYYEARDAY=%d", yd);
		while (yd = bi383_next(&i, &rr->doy), i) {
			fdprintf(",%d", yd);
		}
	}

	with (int d) {
		bitint_iter_t i = 0UL;

		if (!bi31_has_bits_p(rr->dom)) {
			break;
		}
		d = bi31_next(&i, rr->dom);
		fdprintf(";BYMONTHDAY=%d", d);
		while (d = bi31_next(&i, rr->dom), i) {
			fdprintf(",%d", d);
		}
	}

	with (int e) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->easter)) {
			break;
		}
		e = bi383_next(&i, &rr->easter);
		fdprintf(";BYEASTER=%d", e);
		while (e = bi383_next(&i, &rr->easter), i) {
			fdprintf(",%d", e);
		}
	}

	with (struct cd_s cd) {
		bitint_iter_t i = 0UL;

		if (!bi447_has_bits_p(&rr->dow)) {
			break;
		}
		cd = unpack_cd(bi447_next(&i, &rr->dow));
		fdwrite(";BYDAY=", strlenof(";BYDAY="));
		send_cd(whither, cd);
		while (cd = unpack_cd(bi447_next(&i, &rr->dow)), i) {
			fdputc(',');
			send_cd(whither, cd);
		}
	}

	with (int p) {
		bitint_iter_t i = 0UL;

		if (!bi383_has_bits_p(&rr->pos)) {
			break;
		}
		p = bi383_next(&i, &rr->pos);
		fdprintf(";BYPOS=%d", p);
		while (p = bi383_next(&i, &rr->pos), i) {
			fdprintf(",%d", p);
		}
	}

	if (rr->shift) {
		fdprintf(";SHIFT=%c%u", echs_shift_neg_p(rr->shift) ? '-' : '+', echs_shift_absval(rr->shift));
		if (echs_shift_bday_p(rr->shift)) {
			fdputc('B');
			if (echs_shift_inv_p(rr->shift)) {
				fdputc(echs_shift_neg_p(rr->shift) ? '-' : '+');
			}
		}
	}

	if (rr->count >= 0) {
		fdprintf(";COUNT=%zu", rr->count + ccnt);
	}
	if (rr->until.u < -1ULL) {
		char until[32U];
		size_t n;

		n = dt_strf_ical(until, sizeof(until), rr->until);
		fdwrite(";UNTIL=", strlenof(";UNTIL="));
		fdwrite(until, n);
	}

	fdputc('\n');
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

static echs_event_t next_evical_vevent(echs_evstrm_t, bool popp);
static void free_evical_vevent(echs_evstrm_t);
static echs_evstrm_t clone_evical_vevent(echs_const_evstrm_t);
static void send_evical_vevent(int whither, echs_const_evstrm_t s);

static const struct echs_evstrm_class_s evical_cls = {
	.next = next_evical_vevent,
	.free = free_evical_vevent,
	.clone = clone_evical_vevent,
	.seria = send_evical_vevent,
};

static const echs_event_t nul;

static echs_evstrm_t
make_evical_vevent(const struct echs_event_s *ev, size_t nev)
{
	const size_t zev = nev * sizeof(*ev);
	struct evical_s *res;

	if (UNLIKELY((res = malloc(sizeof(*res) + zev)) == NULL)) {
		return NULL;
	}
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
clone_evical_vevent(echs_const_evstrm_t s)
{
	const struct evical_s *this = (const struct evical_s*)s;
	struct evical_s *res;

	res = (struct evical_s*)make_evical_vevent(
		this->ev + this->i, this->nev - this->i);
	return (echs_evstrm_t)res;
}

static echs_event_t
next_evical_vevent(echs_evstrm_t s, bool popp)
{
	struct evical_s *this = (struct evical_s*)s;
	echs_event_t res;

	if (UNLIKELY(this->i >= this->nev)) {
		return nul;
	}
	res = this->ev[this->i];
	if (popp) {
		this->i++;
	}
	return res;
}

static void
send_evical_vevent(int whither, echs_const_evstrm_t s)
{
	const struct evical_s *this = (const struct evical_s*)s;

	if (UNLIKELY(this->i >= this->nev)) {
		return;
	}
	send_ev(whither, this->ev[this->i], 0U);
	return;
}

static echs_instant_t
instant_soup(echs_instant_t broth, echs_instant_t water, echs_tzob_t z, int eof)
{
	echs_instant_t soup;
	echs_tzob_t fz;

	if (UNLIKELY(echs_instant_all_day_p(water))) {
		/* oh we have to paste the missing intra
		 * bits from the proto-event */
		int fof;

		water.intra = broth.intra;
		fof = echs_instant_tzof(water, z);
		soup = echs_instant_detach_tzob(water);

		if (fof != eof) {
			/* we need to add the discrepancy onto from */
			soup = echs_tzob_shift(soup, fof, eof);
		}
	} else if (UNLIKELY((fz = echs_instant_tzob(water)))) {
		soup = echs_instant_utc(water, fz);
	} else {
		soup = water;
	}
	return soup;
}

static echs_evstrm_t
__make_evrdat(echs_event_t e, const echs_instant_t *d, size_t nd)
{
/* this will degrade into an evical_vevent stream */
	struct evical_s *res;
	const size_t zev = nd * sizeof(*res->ev);
	echs_scale_t cal;
	echs_tzob_t z;
	int eof;

	if (nd == 0U) {
		/* not worth it */
		return NULL;
	} else if (UNLIKELY((res = malloc(sizeof(*res) + zev)) == NULL)) {
		/* not possible */
		return NULL;
	}

	/* let the work begin */
	cal = echs_instant_scale(e.from);
	e.from = echs_instant_rescale(e.from, SCALE_GREGORIAN);
	z = echs_instant_tzob(e.from);
	e.from = echs_instant_to_utc(e.from);
	eof = echs_instant_tzof(e.from, z);

	if (nd == 1U) {
		/* no need to sort things, just spread the one instant */
		e.from = instant_soup(e.from, d[0U], z, eof);
		e.from = echs_instant_rescale(e.from, cal);
		res->ev[0U] = e;
	} else {
		/* blimey, use the bottom bit of res->ev to sort the
		 * array of instants D and then spread them out in-situ */
		echs_instant_t *rd =
			(void*)(((uint8_t*)res->ev + zev) - nd * sizeof(*rd));

		/* copy them instants here so we can sort them */
		memcpy(rd, d, nd * sizeof(*rd));
		/* also proto-paste the events in the list and UTCify them
		 * before the actual sorting because in rare cases timezone
		 * changes can actually change the order */
		for (size_t i = 0U; i < nd; i++) {
			rd[i] = instant_soup(e.from, d[i], z, eof);
		}
		/* now sort */
		echs_instant_sort(rd, nd);
		/* now spread out the instants as echs events */
		for (size_t i = 0U; i < nd; i++) {
			e.from = echs_instant_rescale(rd[i], cal);
			res->ev[i] = e;
		}
	}
	/* just the rest of the book-keeping */
	res->class = &evical_cls;
	res->i = 0U;
	res->nev = nd;
	return (echs_evstrm_t)res;
}

static echs_evstrm_t
__make_evvevt(echs_event_t e)
{
	e = echs_event_to_utc(e);
	return make_evical_vevent(&e, 1U);
}


struct evrrul_s {
	echs_evstrm_class_t class;

	/* proto method */
	echs_instruc_t ins;

	/* proto-event */
	echs_event_t e;
	/* proto-zone */
	echs_tzob_t zon;
	/* proto-calscale */
	echs_scale_t cal;
	/* proto-offset */
	int pof;

	/* sequence counter */
	size_t seq;
	/* reference counter */
	size_t ref;

	/* rrul/xrul */
	struct rrulsp_s rrul;

	/* iterator state */
	size_t rdi;
	/* unrolled cache */
	size_t ncch;
	echs_instant_t cch[64U];
};

static echs_event_t next_evrrul(echs_evstrm_t, bool popp);
static void free_evrrul(echs_evstrm_t);
static echs_evstrm_t clone_evrrul(echs_const_evstrm_t);
static void send_evrrul(int whither, echs_const_evstrm_t s);

static const struct echs_evstrm_class_s evrrul_cls = {
	.next = next_evrrul,
	.free = free_evrrul,
	.clone = clone_evrrul,
	.seria = send_evrrul,
};

static echs_evstrm_t
__make_evrrul(echs_event_t e, rrulsp_t rr, size_t nr)
{
/* Mux NR rrules RR carried by event E into one stream. */
	struct evrrul_s *this;
	const size_t duo = sizeof(*this) + sizeof(this);
	struct evrrul_s **that;
	echs_tzob_t zon;

	if (UNLIKELY(nr == 0U)) {
		return NULL;
	} else if (UNLIKELY((this = calloc(nr, duo)) == NULL)) {
		return NULL;
	}
	/* initialise THAT array */
	that = (void*)(this + nr);

	this->class = &evrrul_cls;
	this->cal = echs_instant_scale(e.from);
	e.from = echs_instant_rescale(e.from, SCALE_GREGORIAN);
	this->zon = zon = echs_instant_tzob(e.from);
	this->e = e = echs_event_to_utc(e);
	this->pof = echs_instant_tzof(e.from, zon);

	/* bang the first one */
	this->rrul = rr[0U];
	this->seq = 0U;
	this->ref = nr;
	that[0U] = this;
	/* bang the rest borrowing some fields from the first one */
	for (size_t i = 1U; i < nr; i++) {
		this[i] = this[0U];
		this[i].rrul = rr[i];
		this[i].seq = i;
		that[i] = this + i;
	}
	return echs_evstrm_vmux((const echs_evstrm_t*)that, nr);
}

static void
free_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	if (UNLIKELY(this->seq)) {
		this -= this->seq;
	}
	if (!--this->ref) {
		free(this);
	}
	return;
}

static echs_evstrm_t
clone_evrrul(echs_const_evstrm_t s)
{
	const struct evrrul_s *this = (const struct evrrul_s*)s;
	struct evrrul_s *clon;

	if (UNLIKELY((clon = malloc(sizeof(*this))) == NULL)) {
		return NULL;
	}
	*clon = *this;
	/* clones are not arranged as sequences anymore */
	clon->seq = 0U;
	clon->ref = 1U;
	return (echs_evstrm_t)clon;
}

/* this should be somewhere else, evrrul.c maybe? */
static size_t
refill(struct evrrul_s *restrict strm)
{
/* useful table at:
 * http://icalevents.com/2447-need-to-know-the-possible-combinations-for-repeating-dates-an-ical-cheatsheet/
 * we're trying to follow that one closely. */
	struct rrulsp_s *restrict rr = &strm->rrul;

	assert(rr->freq > FREQ_NONE);
	if (UNLIKELY(echs_nul_instant_p(strm->e.from))) {
		return 0UL;
	} else if (UNLIKELY(!rr->count)) {
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

	if (strm->ncch >= countof(strm->cch)) {
		/* keep one for the next refill */
		strm->e.from = strm->cch[--strm->ncch];
	} else {
		/* take a note that we're at the end of the stream */
		strm->e.from = echs_nul_instant();
	}

	if (rr->count > 0) {
		if (strm->ncch < (size_t)rr->count) {
			rr->count -= strm->ncch;
		} else {
			rr->count = 0;
		}
	}

	if (UNLIKELY(strm->ncch == 0UL)) {
		return 0UL;
	}
	/* convert to target scale */
	for (size_t i = 0U; i < strm->ncch; i++) {
		strm->cch[i] = echs_instant_rescale(strm->cch[i], strm->cal);
	}
	/* utcify them all */
	for (size_t i = 0U; i < strm->ncch; i++) {
		int eof = echs_instant_tzof(strm->cch[i], strm->zon);

		if (UNLIKELY(eof != strm->pof)) {
			/* discrepancy, convert defo */
			strm->cch[i] = echs_tzob_shift(
				strm->cch[i], eof, strm->pof);
		}
	}
	/* otherwise sort the array, just in case */
	echs_instant_sort(strm->cch, strm->ncch);
	return strm->ncch;
}

static echs_event_t
next_evrrul(echs_evstrm_t s, bool popp)
{
	struct evrrul_s *restrict this = (struct evrrul_s*)s;
	echs_event_t res;

	/* it's easier when we just have some precalc'd rdates */
	if (this->rdi >= this->ncch) {
		/* we have to refill the rdate cache */
		if (refill(this) == 0UL) {
			goto nul;
		}
		/* reset counter */
		this->rdi = 0U;
	}
	/* construct the result */
	res = this->e;
	res.from = this->cch[this->rdi];
	if (popp) {
		this->rdi++;
	}
	return res;
nul:
	return nul;
}

static void
send_evrrul(int whither, echs_const_evstrm_t s)
{
	const struct evrrul_s *this = (const struct evrrul_s*)s;

	/* we know rrules are consecutive so only print the DTSTAMP/DTEND
	 * stuff for the first stream in the sequence
	 * also, we have to mimic evmux's next finder as we can't use it
	 * directly because of constness */
	if (!this->seq) {
		echs_event_t e = this->e;

		for (size_t i = 0U; i < this->ref; i++) {
			echs_instant_t cand = this[i].e.from;

			if (UNLIKELY(this[i].rdi >= this[i].ncch)) {
				/* end of stream innit or we need to refill
				 * but this stream is const so just use the
				 * proto event */
				;
			} else {
				cand = this[i].cch[this[i].rdi];
			}
			if (echs_instant_lt_p(cand, e.from)) {
				e.from = cand;
			} else if (echs_nul_instant_p(e.from)) {
				e.from = cand;
			}
		}
		send_ev(whither, e, this->zon);
	}
	send_rrul(whither, &this->rrul, this->ncch - this->rdi);
	return;
}


/* decl'd in evmrul.h, impl'd by us */
void
mrulsp_icalify(int whither, const mrulsp_t *mr)
{
	static const char *const mdirs[] = {
		NULL, "PAST", "PASTTHENFUTURE", "FUTURE", "FUTURETHENPAST",
	};

	fdbang(whither);
	fdwrite("X-GA-MRULE:", 11U);
	if (mr->mdir) {
		fdprintf("DIR=%s", mdirs[mr->mdir]);
	}
	if (mr->from) {
		fdwrite(";MOVEFROM=", 10U);
		send_stset(whither, mr->from);
	}
	if (mr->into) {
		fdwrite(";MOVEINTO=", 10U);
		send_stset(whither, mr->into);
	}

	fdputc('\n');
	return;
}


void
echs_prnt_ical_event(echs_task_t t, echs_event_t ev)
{
	send_ical_hdr(STDOUT_FILENO, t->vtod_typ);
	send_ev(STDOUT_FILENO, ev, 0U);
	send_task(STDOUT_FILENO, t);
	send_ical_ftr(STDOUT_FILENO, t->vtod_typ);
	return;
}

void
echs_prnt_ical_init(void)
{
	static const char hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
CALSCALE:GREGORIAN\n";

	fdbang(STDOUT_FILENO);
	fdwrite(hdr, strlenof(hdr));
	return;
}

void
echs_prnt_ical_fini(void)
{
	static const char ftr[] = "\
END:VCALENDAR\n";

	fdbang(STDOUT_FILENO);
	fdwrite(ftr, strlenof(ftr));
	fdflush();
	return;
}

struct rrulsp_s
echs_read_rrul(const char *s, size_t z)
{
	return snarf_rrule(s, z);
}

echs_evstrm_t
echs_make_evstrm_rrul(echs_instant_t from, struct rrulsp_s r[static 1U], size_t nr)
{
	echs_event_t e = {
		.from = from,
		.dur = 0,
		.oid = 0,
		.sts = 0,
	};

	return __make_evrrul(e, r, nr);
}

static echs_task_t
make_task(struct ical_vevent_s *ve)
{
/* turn our catch-all blob VE into one or more (TASK, STRM) tuples
 * (the former contains the latter) based on slots set in VE */
	struct echs_task_s *res;
	echs_evstrm_t s;

	if (UNLIKELY((res = malloc(sizeof(*res))) == NULL)) {
		return NULL;
	}

	/* generate a uid on the fly */
	if (UNLIKELY(!ve->t.oid)) {
		ve->t.oid = echs_toid_gen(&ve->t);
	}
	/* off-by-one correction of umask, this is to indicate
	 * an unset umask by the value of -1 */
	ve->t.umsk--;
	/* off-by-one correction of max_simul, this is to indicate
	 * an unset max_simul by the value of -1 */
	ve->t.max_simul--;

	with (echs_instant_t i = echs_instant_to_utc(ve->from)) {
		ve->till = echs_instant_to_utc(ve->till);
		/* transform e.from + e.till into e.dur */
		if (echs_nul_idiff_p(ve->dur) &&
		    echs_instant_lt_p(i, ve->till)) {
			/* turn absolute till into duration */
			ve->dur = echs_instant_diff(ve->till, i);
		} else if (ve->dur.d < 0) {
			/* negative durations are bullshit */
			ve->dur = echs_nul_idiff();
		}
	}

	if (!echs_nul_instant_p(ve->comp)) {
		/* this one's a reporting vtodo/vjournal */
		s = NULL;
		ve->t.compl = ve->comp;
		ve->t.vtod_typ = VTOD_TYP_COMPL;

	} else if (echs_nul_instant_p(ve->from)) {
		/* oooh must be one of them execution vtodos */
		s = NULL;

		if (!echs_nul_idiff_p(ve->dur)) {
			ve->t.timeout = ve->dur;
			ve->t.vtod_typ = VTOD_TYP_TIMEOUT;
		} else if (!echs_nul_instant_p(ve->due)) {
			ve->t.due = ve->due;
			ve->t.vtod_typ = VTOD_TYP_DUE;
		}
	} else if (!ve->rrul.nr && !ve->rdat.ndt) {
		/* not an rrule but a normal vevent */
		/* free all the bits and bobs that
		 * might have been added */
		echs_event_t e = {
			.from = echs_instant_rescale(ve->from, ve->cal),
			.dur = ve->dur,
			.oid = ve->t.oid,
			.sts = ve->sts,
		};

		if (ve->xrul.nr) {
			free(ve->xrul.r);
		}
		if (ve->xdat.ndt) {
			free(ve->xdat.dt);
		}
		if (ve->mrul.nr) {
			free(ve->mrul.r);
		}
		assert(ve->rrul.r == NULL);
		assert(ve->rdat.dt == NULL);
		s = __make_evvevt(e);
	} else {
		/* it's an rrule */
		echs_event_t e = {
			.from = echs_instant_rescale(ve->from, ve->cal),
			.dur = ve->dur,
			.oid = ve->t.oid,
			.sts = ve->sts,
		};
		/* rrule stream (struct evrrul_s* really) */
		echs_evstrm_t sr;
		/* exception stream */
		echs_evstrm_t sx;

		/* get a proto exrule stream, composed of all exrules
		 * in a nicely evmux'd stream */
		with (echs_evstrm_t xr, x1) {
			xr = __make_evrrul(e, ve->xrul.r, ve->xrul.nr);
			x1 = __make_evrdat(e, ve->xdat.dt, ve->xdat.ndt);

			if (xr != NULL && x1 != NULL) {
				/* mux them into one */
				sx = echs_evstrm_mux(xr, x1, NULL);
			} else if (xr != NULL) {
				sx = xr;
			} else {
				sx = x1;
			}
		}

		with (echs_evstrm_t rr, r1) {
			rr = __make_evrrul(e, ve->rrul.r, ve->rrul.nr);
			r1 = __make_evrdat(e, ve->rdat.dt, ve->rdat.ndt);

			if (rr != NULL && r1 != NULL) {
				/* mux them into one */
				sr = echs_evstrm_mux(rr, r1, NULL);
			} else if (rr != NULL) {
				sr = rr;
			} else {
				sr = r1;
			}
		}

		/* free all resources in the vevent */
		free_ical_vevent(ve);

		/* special mux this one is, for the SX is an exception
		 * stream, and SR is known to be a */
		s = make_evfilt(sr, sx);
	}
	/* now massage the task specific fields in VE into an echs_task_t */
	*res = ve->t;
	res->strm = s;
	return res;
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

echs_instruc_t
echs_evical_pull(ical_parser_t p[static 1U])
{
	struct ical_vevent_s *ve;
	echs_instruc_t i = {INSVERB_UNK};

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
	} else {
		struct ical_parser_s *_p = *p;

		switch (_p->globve.meth) {
		case METH_UNK:
		case METH_PUBLISH:
		case METH_REQUEST:
			i.v = INSVERB_SCHE;
			i.t = make_task(ve);
			break;
		case METH_REPLY:
			i.o = ve->t.oid;
			switch (ve->req_status) {
			case 2U:
				i.v = INSVERB_RESC;
				break;
			case 5U:
				i.v = INSVERB_UNSC;
				break;
			default:
				break;
			}
			break;
		case METH_CANCEL:
			i.v = INSVERB_UNSC;
			i.o = ve->t.oid;
			i.rng = (echs_range_t){ve->from, ve->till};
			break;
		default:
		case METH_ADD:
		case METH_REFRESH:
		case METH_COUNTER:
		case METH_DECLINECOUNTER:
			break;
		}
	}
	return i;
}

echs_instruc_t
echs_evical_last_pull(ical_parser_t p[static 1U])
{
	echs_instruc_t res = echs_evical_pull(p);

	if (LIKELY(*p != NULL)) {
		_ical_fini(*p);
		free(*p);
	}
	return res;
}


/* seria/deseria helpers */
void
echs_task_icalify(int whither, echs_task_t t)
{
	echs_evstrm_t s;

	if (UNLIKELY((s = t->strm) == NULL)) {
		/* do fuckall */
		;
	} else if (UNLIKELY(echs_nul_event_p(echs_evstrm_next(s)))) {
		/* doesn't make sense to put this guy on the wire does it */
		return;
	}

	send_ical_hdr(whither, s == NULL);
	send_task(whither, t);
	if (s != NULL) {
		echs_evstrm_seria(whither, s);
	}
	send_ical_ftr(whither, s == NULL);
	return;
}

void
echs_unsc_icalify(int whither, const char *tuid)
{
	static const char sta[] = "STATUS:CANCELLED\n";

	fdbang(whither);
	send_ical_hdr(whither, false);
	fdprintf("UID:%s\n", tuid);
	fdwrite(sta, strlenof(sta));
	send_ical_ftr(whither, false);
	return;
}

void
echs_icalify_init(int whither, echs_instruc_t i)
{
	static const char hdr[] = "\
BEGIN:VCALENDAR\n\
VERSION:2.0\n\
PRODID:-//GA Financial Solutions//echse//EN\n\
CALSCALE:GREGORIAN\n";

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);
	/* definitely the head of the header */
	fdwrite(hdr, strlenof(hdr));

	switch (i.v/*erb*/) {
		static const char meth_crea[] = "METHOD:PUBLISH\n";
		static const char meth_unsc[] = "METHOD:CANCEL\n";

	case INSVERB_SCHE:
		fdwrite(meth_crea, strlenof(meth_crea));
		if (UNLIKELY(i.t == NULL)) {
			break;
		}
		/* use specifics in T to declare defaults */
		if (i.t->max_simul) {
			fdprintf("X-ECHS-MAX-SIMUL:%d\n", i.t->max_simul);
		}
		with (nummapstr_t o = i.t->owner) {
			const char *p;
			uintptr_t n;

			if ((p = nummapstr_str(o))) {
				fdprintf("X-ECHS-OWNER:%s\n", p);
			} else if ((n = nummapstr_num(o)) < NUMMAPSTR_NAN &&
				   (unsigned int)n < -1U) {
				fdprintf("X-ECHS-OWNER:%u\n", (unsigned int)n);
			}
		}
		break;
	case INSVERB_UNSC:
		fdwrite(meth_unsc, strlenof(meth_unsc));
		break;
	default:
		break;
	}

	/* no flushing this one as the user is expected to call
	 * `echs_icalify_fini()' when the time is ripe */
	return;
}

void
echs_icalify_fini(int whither)
{
	static const char ftr[] = "\
END:VCALENDAR\n";

	/* tell the bufferer we want to write to WHITHER */
	fdbang(whither);
	/* just the footer please */
	fdwrite(ftr, strlenof(ftr));
	/* that's the last thing in line, just send it off */
	fdflush();
	return;
}

/* evical.c ends here */
