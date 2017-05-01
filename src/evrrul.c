/*** evrrul.c -- recurrence rules
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
#include <stdlib.h>
#include <string.h>
#include "evrrul.h"
#include "nifty.h"

struct md_s {
	unsigned int m;
	unsigned int d;
};

static const unsigned int mdays[] = {
	0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

#define M	(MON)
#define T	(TUE)
#define W	(WED)
#define R	(THU)
#define F	(FRI)
#define A	(SAT)
#define S	(SUN)

static const echs_wday_t __jan01_28y_wday[] = {
	/* 1904 - 1910 */
	F, S, M, T, W, F, A,
	/* 1911 - 1917 */
	S, M, W, R, F, A, M,
	/* 1918 - 1924 */
	T, W, R, A, S, M, T,
	/* 1925 - 1931 */
	R, F, A, S, T, W, R,
};

#undef M
#undef T
#undef W
#undef R
#undef F
#undef A
#undef S

/* we can enumerate the cross product of time components */
struct enum_s {
	size_t nel;
	uint8_t nH, nM, nS;
	uint8_t H[24U];
	uint8_t M[60U];
	uint8_t S[60U];
};

#define ENUM_INIT(e, s, ...)		size_t s = 0U, ENUM_INIT_M(e, ## __VA_ARGS__, auto_m)
#define ENUM_INIT_M(e, m, ...)		m = 0U, ENUM_INIT_H(e, ## __VA_ARGS__, auto_h)
#define ENUM_INIT_H(e, h, ...)		h = 0U
#define ENUM_COND(e, s, ...)		((s) < (e).nS || ((s = 0U), ENUM_COND_M(e, ## __VA_ARGS__, auto_m)))
#define ENUM_COND_M(e, m, ...)		((m)++, (m) < (e).nM || ((m = 0U), ENUM_COND_H(e, ## __VA_ARGS__, auto_h)))
#define ENUM_COND_H(e, h, ...)		((h)++, (h) < (e).nH || (h = 0U))
#define ENUM_ITER(e, s, ...)		(s)++, ENUM_ITER_M(e, ## __VA_ARGS__, auto_m = -2U)
#define ENUM_ITER_M(e, m, ...)		(m), ENUM_ITER_H(e, ## __VA_ARGS__, auto_h = -2U)
#define ENUM_ITER_H(e, h, ...)		(h)


/* generic date converters */
static __attribute__((const, pure)) echs_wday_t
ymd_get_wday(unsigned int y, unsigned int m, unsigned int d)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int res;

	y -= m < 3;
	res = y + y / 4U - y / 100U + y / 400U;
	res += t[m - 1U] + d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}

static __attribute__((const, pure)) echs_wday_t
yd_get_wday(unsigned int y, unsigned int yd)
{
	echs_wday_t j01 = ymd_get_wday(y, 1U, 1U);
	return (echs_wday_t)(((j01 + --yd) % 7U) ?: SUN);
}

static __attribute__((const, pure)) unsigned int
ymd_get_yd(unsigned int y, unsigned int m, unsigned int d)
{
/* stolen from dateutils (__md_get_yday()) */
	static uint16_t __mon_yday[] = {
		/* this is \sum ml,
		 * first element is a bit set of leap days to add */
		0xfff8, 0,
		31, 59, 90, 120, 151, 181,
		212, 243, 273, 304, 334, 365
	};
	return __mon_yday[m] + d + UNLIKELY(!(y % 4U)/*leapp(year)*/ && m >= 3);
}

static __attribute__((const, pure)) inline unsigned int
__get_ndom(unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y. */
	unsigned int res = mdays[m];

	if (UNLIKELY(!(y % 4U) && m == 2U)) {
		res++;
	}
	return res;
}

static int
__get_mcnt(unsigned int y, unsigned int m, echs_wday_t w)
{
/* get the number of weekdays W in Y-M, which is the max count
 * for a weekday W in ymcw dates in year Y and month M */
	echs_wday_t wd1 = ymd_get_wday(y, m, 1U);
	unsigned int md = __get_ndom(y, m);
	/* the maximum number of WD01s in Y-M */
	unsigned int wd01cnt = (md - 1U) / 7U + 1;
	/* modulus */
	unsigned int wd01mod = (md - 1U) % 7U;

	/* we need sun on 0 */
	if (w == SUN) {
		w = MIR;
	}
	/* now the next WD01MOD days also have WD01CNT occurrences
	 * if wd01 + wd01mod exceeds the DAYS_PER_WEEK barrier wrap
	 * around by extending W to W + DAYS_PER_WEEK */
	if ((w >= wd1 && w <= wd1 + wd01mod) ||
	    (w + 7U) <= wd1 + wd01mod) {
		return wd01cnt;
	} else {
		return wd01cnt - 1;
	}
}

static unsigned int
ymcw_get_dom(unsigned int y, unsigned int m, int c, echs_wday_t w)
{
/* convert instant with CW coded D slot in Y to Gregorian insants. */
	/* get month's first's weekday */
	echs_wday_t wd1 = ymd_get_wday(y, m, 1U);
	unsigned int max = __get_mcnt(y, m, w);
	unsigned int add;
	unsigned int tgtd;

	/* quick sanity check */
	if (c > (int)max) {
		return 0U;
	} else if (c < 0 && (c += max + 1) <= 0) {
		return 0U;
	}

	/* now just like in the __wday_after() code, we want the number of days
	 * to add so that wd(res + X) == W above,
	 * this is a simple modulo subtraction */
	add = ((unsigned int)w + 7U - (unsigned int)wd1) % 7U;
	if ((tgtd = 1U + add + (c - 1) * 7U) > mdays[m]) {
		if (UNLIKELY(tgtd == 29U && !(y % 4U))) {
			/* ah, leap year innit
			 * no need to check for Feb because months are
			 * usually longer than 29 days and we wouldn't be
			 * here if it was a different month */
			;
		} else {
			tgtd -= 7U;
		}
	}
	return tgtd;
}

static inline __attribute__((pure)) echs_wday_t
get_jan01_wday(unsigned int year)
{
/* get the weekday of jan01 in YEAR
 * using the 28y cycle thats valid till the year 2399
 * 1920 = 16 mod 28 */
	return __jan01_28y_wday[year % 28U];
}

static int
ywd_get_jan01_hang(echs_wday_t j01)
{
/* Mon means hang of 0, Tue -1, Wed -2, Thu -3, Fri 3, Sat 2, Sun 1 */
	int res;

	if (UNLIKELY((res = 1 - (int)j01) < -3)) {
		return (int)(7U + res);
	}
	return res;
}

static inline __attribute__((const, pure))  unsigned int
get_isowk(unsigned int y)
{
	switch (y % 28U) {
	default:
		break;
	case 16:
		/* 1920, 1948, ... */
	case 21:
		/* 1925, 1953, ... */
	case 27:
		/* 1931, 1959, ... */
	case 4:
		/* 1936, 1964, ... */
	case 10:
		/* 1942, 1970, ... */
		return 53;
	}
	return 52;
}

static unsigned int
ywd_get_yday(unsigned int y, int w, int d)
{
/* since everything is in ISO 8601 format, getting the doy is a matter of
 * counting how many days there are in a week. */
	/* this one's special as it needs the hang helper slot */
	echs_wday_t j01 = get_jan01_wday(y);
	int hang = ywd_get_jan01_hang(j01);

	if (UNLIKELY(w < 0)) {
		w += 1 + get_isowk(y);
	}
	return 7U * (w - 1) + d + hang;
}

static struct md_s
yd_to_md(unsigned int y, int doy)
{
/* Freundt algo, stolen from dateutils */
#define GET_REM(x)	(rem[x])
	static const uint8_t rem[] = {
		19, 19, 18, 14, 13, 11, 10, 8, 7, 6, 4, 3, 1, 0

	};
	unsigned int m;
	unsigned int d;
	unsigned int beef;
	unsigned int cake;

	if (UNLIKELY(doy < 0)) {
		doy += 366 + !(y % 4U);
	}

	/* get 32-adic doys */
	m = (doy + 19) / 32U;
	d = (doy + 19) % 32U;
	beef = GET_REM(m);
	cake = GET_REM(m + 1);

	/* put leap years into cake */
	if (UNLIKELY(!(y % 4U)/*leapp(y)*/ && cake < 16U)) {
		/* note how all leap-affected cakes are < 16 */
		beef += beef < 16U;
		cake++;
	}

	if (d <= cake) {
		d = doy - ((m - 1) * 32 - 19 + beef);
	} else {
		d = doy - (m++ * 32 - 19 + cake);
	}
	return (struct md_s){m, d};
#undef GET_REM
}

static struct md_s
ywd_to_md(unsigned int y, int w, echs_wday_t d)
{
	unsigned int yday = ywd_get_yday(y, w, d);
	struct md_s res = yd_to_md(y, yday);

	if (UNLIKELY(res.m == 0)) {
		res.m = 12;
		res.d--;
	} else if (UNLIKELY(res.m == 13)) {
		res.m = 1;
	}
	return res;
}

static unsigned int
ycw_get_yday(unsigned int y, int c, echs_wday_t w)
{
/* this differs from ywd in that we're actually looking for the C-th
 * occurrence of W in year Y
 * this is the inverse of dateutils' ymcw.c:__ymcw_get_yday()
 * look there for details */
	echs_wday_t j01w = ymd_get_wday(y, 1, 1);
	unsigned int diff = j01w <= w ? w - j01w : 7 + w - j01w;

	/* if W == j01w, the first W is the first yday
	 * the second W is the 8th yday, etc.
	 * so the first W is on 1 + diff */
	if (c > 0) {
		return 7 * (c - 1) + diff + 1;
	} else if (c < 0) {
		/* similarly for negative c,
		 * there's always the 53rd J01 in Y,
		 * mapping to the 365th day */
		unsigned int res = 7U * (53 + c) + diff + 1U;

		switch (diff) {
		default:
			break;
		case 0:
			return res;
		case 1:
			if (UNLIKELY(!(y % 4U)/*leap year*/)) {
				return res;
			}
			break;
		}
		return res - 7;
	}
	/* otherwise it's bullshit */
	return 0U;
}

static unsigned int
easter_get_yday(unsigned int y)
{
/* calculate gregorian easter date */
	unsigned int a = y % 19U;
	unsigned int b = y / 4U;
	unsigned int c = b / 25U + 1;
	unsigned int d = 3U * c / 4U;
	unsigned int e;

	e = 19U * a + -((8U * c + 5) / 25U) + d + 15U;
	e %= 30U;
	e += (29578U - a - 32U * e) / 1024U;
	e = e - ((y % 7U) + b - d + e + 2) % 7U;
	/* e is expressed in terms of Mar, so add the days till 01/Mar
	 * to obtain the doy */
	return e + 59 + !(y % 4U);
}

static unsigned int
pack_cand(unsigned int m, unsigned int d)
{
	return (--m) * 32U + d;
}

static struct md_s
unpack_cand(unsigned int c)
{
	return (struct md_s){c / 32U + 1U, c % 32U};
}

static inline struct md_s
inc_md(struct md_s md, const unsigned int y)
{
	if (UNLIKELY(++md.d > __get_ndom(y, md.m))) {
		md.d = 1U;
		md.m++;
	}
	return md;
}

static inline echs_wday_t
inc_wd(echs_wday_t wd)
{
	if (UNLIKELY(!(++wd % 8U))) {
		wd = MON;
	}
	return wd;
}

static int
make_enum(struct enum_s *restrict tgt, echs_instant_t proto, rrulsp_t rr)
{
	size_t nH = 0U;
	size_t nM = 0U;
	size_t nS = 0U;
	unsigned int tmp;

	/* get all hours */
	for (bitint_iter_t Hi = 0UL;
	     (tmp = bui31_next(&Hi, rr->H), Hi);) {
		tgt->H[nH++] = (uint8_t)tmp;
	}
	if (!nH) {
		tgt->H[nH++] = (uint8_t)proto.H;
	}
	/* get all minutes */
	for (bitint_iter_t Mi = 0UL;
	     (tmp = bui63_next(&Mi, rr->M), Mi);) {
		tgt->M[nM++] = (uint8_t)tmp;
	}
	if (!nM) {
		tgt->M[nM++] = (uint8_t)proto.M;
	}
	/* get all them seconds */
	for (bitint_iter_t Si = 0UL;
	     (tmp = bui63_next(&Si, rr->S), Si);) {
		tgt->S[nS++] = (uint8_t)tmp;
	}
	if (!nS) {
		tgt->S[nS++] = (uint8_t)proto.S;
	}
	tgt->nH = nH;
	tgt->nM = nM;
	tgt->nS = nS;
	return 0;
}

static bool
md_match_p(struct md_s md, bituint31_t m, bitint31_t d)
{
	const bool mp = bui31_has_bits_p(m);
	const bool dp = bi31_has_bits_p(d);

	if (LIKELY(!mp && !dp)) {
		return true;
	} else if (mp && dp) {
		return bui31_has_bit_p(m, md.m) &&
			bi31_has_bit_p(d, md.d);
	} else if (mp) {
		return bui31_has_bit_p(m, md.m);
	} else if (dp) {
		return bi31_has_bit_p(d, md.d);
	}
	return false;
}


/* recurrence helpers */
static void
fill_yly_ywd(
	bitint383_t *restrict cand, unsigned int y,
	const bitint63_t woy, const bitint447_t *dow)
{
	int wk;

	for (bitint_iter_t wki = 0UL;
	     (wk = bi63_next(&wki, woy), wki);) {
		/* ywd */
		int dc;

		for (bitint_iter_t dowi = 0UL;
		     (dc = bi447_next(&dowi, dow), dowi);) {
			struct md_s md;
			echs_wday_t wd;

			if (dc <= MIR || (wd = (echs_wday_t)dc) > SUN) {
				continue;
			} else if (!(md = ywd_to_md(y, wk, wd)).m) {
				continue;
			}
			/* otherwise it's looking good */
			ass_bi383(cand, pack_cand(md.m, md.d));
		}
	}
	return;
}

static void
fill_mly_ymcw(
	bitint383_t *restrict cand,
	const unsigned int y, const unsigned int m, const bitint447_t *dow)
{
	int tmp;

	for (bitint_iter_t dowi = 0UL;
	     (tmp = bi447_next(&dowi, dow), dowi);) {
		unsigned int dom;
		struct cd_s cd = unpack_cd(tmp);

		if (cd.cnt == 0 ||
		    !(dom = ymcw_get_dom(y, m, cd.cnt, cd.dow))) {
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, pack_cand(m, dom));
	}
	return;
}

static void
fill_yly_ymcw(
	bitint383_t *restrict cand, unsigned int y,
	const bitint447_t *dow, const unsigned int m[static 12U], size_t nm)
{
	for (size_t i = 0UL; i < nm; i++) {
		fill_mly_ymcw(cand, y, m[i], dow);
	}
	return;
}

static void
fill_yly_ycw(bitint383_t *restrict cand, unsigned int y, const bitint447_t *dow)
{
	int tmp;

	for (bitint_iter_t dowi = 0UL;
	     (tmp = bi447_next(&dowi, dow), dowi);) {
		struct cd_s cd = unpack_cd(tmp);
		struct md_s md;
		unsigned int yd;

		if (cd.cnt == 0) {
			continue;
		} else if (!(yd = ycw_get_yday(y, cd.cnt, cd.dow))) {
			continue;
		} else if (!(md = yd_to_md(y, yd)).m) {
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, pack_cand(md.m, md.d));
	}
	return;
}

static void
fill_yly_yd(
	bitint383_t *restrict cand, unsigned int y,
	const bitint383_t *doy, uint8_t wd_mask)
{
	int yd;

	for (bitint_iter_t doyi = 0UL;
	     (yd = bi383_next(&doyi, doy), doyi);) {
		/* yd */
		struct md_s md;

		if (wd_mask >> 1U &&
		    !((wd_mask >> yd_get_wday(y, yd)) & 0b1U)) {
			/* weekday is masked out */
			continue;
		} else if (!(md = yd_to_md(y, yd)).m) {
			/* something's wrong again */
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, pack_cand(md.m, md.d));
	}
	return;
}

static void
fill_yly_yd_all(bitint383_t *restrict c, const unsigned int y, uint8_t wd_mask)
{
/* the dense verison of fill_yly_yd() where all days in DOY are set */
	struct md_s md = {1U, 1U};

	if (UNLIKELY(!(wd_mask >> 1U))) {
		/* quick exit
		 * lowest bit doesn't count as it indicates
		 * non strictly-weekdays (i.e. non-0 counts) in the bitint */
		return;
	}

	/* just go through all days of the year keeping track of
	 * the week day*/
	for (unsigned int yd = 1U, w = ymd_get_wday(y, 1U, 1U),
		     nyd = (y % 4U) ? 365U : 366U;
	     yd <= nyd; yd++, w = inc_wd((echs_wday_t)w), md = inc_md(md, y)) {
		if (!((wd_mask >> w) & 0b1U)) {
			/* weekday is masked out */
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(c, pack_cand(md.m, md.d));
	}
	return;
}

static void
fill_yly_md_all(
	bitint383_t *restrict c, const unsigned int y,
	const unsigned int m[static 12U], size_t nm, uint8_t wd_mask)
{
/* the dense version of fill_yly_ymd() where all days in M are set */
	if (UNLIKELY(!(wd_mask >> 1U))) {
		/* quick exit
		 * lowest bit doesn't count as it indicates
		 * non strictly-weekdays (i.e. non-0 counts) in the bitint */
		return;
	}

	for (size_t i = 0U; i < nm; i++) {
		/* just go through all days of the month keeping track of
		 * the week day*/
		const unsigned int nmd = __get_ndom(y, m[i]);

		for (unsigned int md = 1U, w = ymd_get_wday(y, m[i], 1U);
		     md <= nmd; md++, w = inc_wd((echs_wday_t)w)) {
			if (!((wd_mask >> w) & 0b1U)) {
				/* weekday is masked out */
				continue;
			}
			/* otherwise it's looking good */
			ass_bi383(c, pack_cand(m[i], md));
		}
	}
	return;
}

static void
fill_yly_eastr(
	bitint383_t *restrict cand,
	unsigned int y, const bitint383_t *s,
	bituint31_t m, bitint31_t d,
	uint8_t wd_mask)
{
	int offs;

	for (bitint_iter_t easteri = 0UL;
	     (offs = bi383_next(&easteri, s), easteri);) {
		/* easter offset calendar */
		unsigned int yd;
		struct md_s md;

		if (wd_mask >> 1U) {
			if (offs >= 0 &&
			    !((wd_mask >> (offs % 7U)) & 0b1U)) {
				continue;
			} else if (offs < 0 &&
				   !((wd_mask >> (7 - (-offs % 7U))) & 0b1U)) {
				continue;
			}
		}
		if (!(yd = easter_get_yday(y))) {
			continue;
		} else if (!(yd += offs) || yd > 366) {
			/* huh? */
			continue;
		} else if (!(md = yd_to_md(y, yd)).m) {
			continue;
		} else if (!md_match_p(md, m, d)) {
			/* can't use this one, user wants it masked */
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, pack_cand(md.m, md.d));
	}
	return;
}

static void
fill_mly_ymd(
	bitint383_t *restrict cand,
	const unsigned int y, const unsigned int mo,
	const int d[static 2U * 31U], size_t nd,
	uint8_t wd_mask)
{
	for (size_t j = 0UL; j < nd; j++) {
		unsigned int ndom = __get_ndom(y, mo);
		int dd = d[j];

		if (dd > 0 && (unsigned int)dd <= ndom) {
			;
		} else if (dd < 0 && ndom + 1U + dd > 0) {
			dd += ndom + 1U;
		} else {
			continue;
		}
		/* check wd_mask */
		if (wd_mask >> 1U &&
		    !((wd_mask >> ymd_get_wday(y, mo, dd)) & 0b1U)) {
			continue;
		}

		/* it's a candidate */
		ass_bi383(cand, pack_cand(mo, dd));
	}
	return;
}

#if 0
static void
fill_mly_hij(
	bitint383_t *restrict cand,
	const unsigned int hy, const unsigned int hm,
	const int hd[static 2U * 31U], size_t nhd,
	bituint31_t mon, bitint31_t dom,
	uint8_t wd_mask)
{
	for (size_t j = 0UL; j < nhd; j++) {
		const unsigned int ndohm =
			echs_scale_ndim(hy, hm, SCALE_HIJRI_UMMULQURA);
		int dd = hd[j];
		jd_t jd;
		struct ymd_s g;

		if (dd > 0 && (unsigned int)dd <= ndohm) {
			;
		} else if (dd < 0 && ndohm + 1U + dd > 0) {
			dd += ndohm + 1U;
		} else {
			continue;
		}
		/* convert to JDN */
		if (UNLIKELY(!(jd = h2jd((struct ymd_s){hy, hm, dd})))) {
			continue;
		}

		/* check wd_mask */
		if (wd_mask >> 1U &&
		    !((wd_mask >> jd_get_wday(jd)) & 0b1U)) {
			/* nope, it's filtered out */
			continue;
		}

		/* convert to gregorian dates */
		g = jd2g(jd);

		/* match against gregorian dates */
		if (!md_match_p((struct md_s){g.m, g.d}, mon, dom)) {
			/* can't use this one, user wants it masked */
			continue;
		}

		/* it's a candidate */
		ass_bi383(cand, pack_cand(g.m, g.d));
	}
	return;
}
#endif

static void
fill_yly_ymd(
	bitint383_t *restrict cand, unsigned int y,
	const unsigned int m[static 12U], size_t nm,
	const int d[static 2U * 31U], size_t nd,
	uint8_t wd_mask)
{
	for (size_t i = 0UL; i < nm; i++) {
		fill_mly_ymd(cand, y, m[i], d, nd, wd_mask);
	}
	return;
}

#if 0
static void
fill_yly_hij(
	bitint383_t *restrict cand, unsigned int hy,
	const unsigned int hm[static 12U], size_t nm,
	const int hd[static 2U * 31U], size_t nd,
	bituint31_t mon, bitint31_t dom,
	uint8_t wd_mask)
{
	for (size_t i = 0UL; i < nm; i++) {
		fill_mly_hij(cand, hy, hm[i], hd, nd, mon, dom, wd_mask);
	}
	return;
}
#endif

static void
fill_yly_ymd_all_m(
	bitint383_t *restrict cand, unsigned int y,
	const int d[static 2U * 31U], size_t nd,
	uint8_t wd_mask)
{
	for (unsigned int m = 1U; m <= 12U; m++) {
		for (size_t j = 0UL; j < nd; j++) {
			const unsigned int ndom = __get_ndom(y, m);
			int dd = d[j];

			if (dd > 0 && (unsigned int)dd <= ndom) {
				;
			} else if (dd < 0 && ndom + 1U + dd > 0) {
				dd += ndom + 1U;
			} else {
				continue;
			}
			/* check wd_mask */
			if (wd_mask &&
			    !((wd_mask >> ymd_get_wday(y, m, dd)) & 0b1U)) {
				continue;
			}

			/* it's a candidate */
			ass_bi383(cand, pack_cand(m, dd));
		}
	}
	return;
}

static void
fill_mly_ymd_all_d(
	bitint383_t *restrict cand,
	const unsigned int y, const unsigned int mo, uint8_t wd_mask)
{
	const unsigned int ndom = __get_ndom(y, mo);

	for (unsigned int dd = 1U, w = ymd_get_wday(y, mo, dd);
	     dd <= ndom; dd++, w = inc_wd((echs_wday_t)w)) {
		/* check wd_mask */
		if (wd_mask &&
		    !((wd_mask >> w) & 0b1U)) {
			continue;
		}

		/* it's a candidate */
		ass_bi383(cand, pack_cand(mo, dd));
	}
	return;
}

static void
fill_yly_ymd_all_d(
	bitint383_t *restrict cand, unsigned int y,
	const unsigned int m[static 12U], size_t nm,
	uint8_t wd_mask)
{
	for (size_t i = 0UL; i < nm; i++) {
		fill_mly_ymd_all_d(cand, y, m[i], wd_mask);
	}
	return;
}

static void
clr_poss(bitint383_t *restrict cand, const bitint383_t *poss)
{
	bitint383_t res = {0U};
	bitint_iter_t ci = 0UL;
	int pos;
	int prev = 0;
	unsigned int nbits = 0U;

	if (!bi383_has_bits_p(poss)) {
		/* nothing to do */
		return;
	}
	for (bitint_iter_t posi = 0UL;
	     (pos = bi383_next(&posi, poss), posi); prev = pos) {
		int c = 0;

		if (pos < 0) {
			if (!nbits) {
				/* quickly count them bits, singleton */
				for (bitint_iter_t cnti = 0UL;
				     (bi383_next(&cnti, cand), cnti); nbits++);
			}
			pos = nbits + pos + 1;
		}
		if (prev > pos) {
			/* reset ci */
			ci = 0UL;
			prev = 0;
		}
		/* just shave bits off of cand */
		for (int p = pos - prev;
		     p > 0 && (c = bi383_next(&ci, cand), ci); p--);
		/* assign if successful */
		if (LIKELY(c > 0)) {
			ass_bi383(&res, c);
		}
	}
	/* copy res over */
	*cand = res;
	return;
}

static void
add_poss(bitint383_t *restrict cand, const unsigned int y, const bitint383_t *p)
{
	bitint383_t res = {0U};
	int add;

	if (!bi383_has_bits_p(p)) {
		/* nothing to add */
		return;
	}
	/* go through summands ... */
	for (bitint_iter_t posi = 0UL; (add = bi383_next(&posi, p), posi);) {
		int c;

		if (UNLIKELY(add == 0)) {
			/* ah, copying requested then */
			res = *cand;
			continue;
		}

		/* and here, go through candidates and add summand ADD */
		for (bitint_iter_t ci = 0UL; (c = bi383_next(&ci, cand), ci);) {
			const struct md_s md = unpack_cand(c);
			int nu_d = md.d + add;
			int nu_m = md.m;

		reassess:
			if (UNLIKELY(nu_m <= 0 || nu_m > 12)) {
				/* this is for fixups going wrong */
				continue;
			} else if (UNLIKELY(nu_d <= 0)) {
				/* fixup, innit */
				nu_d += __get_ndom(y, --nu_m);
				goto reassess;
			} else if (UNLIKELY(nu_d > (int)__get_ndom(y, nu_m))) {
				/* fixup too, grrr */
				nu_d -= __get_ndom(y, nu_m++);
				goto reassess;
			}
			/* otherwise, we can assign directly */
			ass_bi383(&res, pack_cand(nu_m, nu_d));
		}
	}
	/* copy res over */
	*cand = res;
	return;
}

size_t
rrul_fill_yly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	/* unrolled month bui31 bitset */
	unsigned int m[12U];
	size_t nm;
	/* unrolled day bi31, we use 2 * 31 because by monthdays can
	 * also be denoted negatively, thus 1, -1, 2, -2, ..., 31, -31 is
	 * the biggest possible BYMONTHDAY value */
	int d[2U * 31U];
	size_t nd;
	size_t res = 0UL;
	size_t tries;
	uint8_t wd_mask = 0U;
	bool ymdp;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ymd only */
	ymdp = !bi63_has_bits_p(rr->wk) &&
		!bi447_has_bits_p(&rr->dow) &&
		!bi383_has_bits_p(&rr->doy) &&
		!bi383_has_bits_p(&rr->easter) &&
		!bi31_has_bits_p(rr->dom);

	/* generate a set of minutes and seconds */
	(void)make_enum(&e, proto, rr);

	with (unsigned int tmpm) {
		nm = 0UL;
		for (bitint_iter_t mi = 0U;
		     nm < countof(m) && (tmpm = bui31_next(&mi, rr->mon), mi);
		     m[nm++] = tmpm);

		/* fill up with a default */
		if (!nm && ymdp && proto.m) {
			m[nm++] = proto.m;
		}
	}
	with (int tmpd) {
		nd = 0UL;
		for (bitint_iter_t di = 0U;
		     nd < nti && (tmpd = bi31_next(&di, rr->dom), di);
		     d[nd++] = tmpd);

		/* fill up with the default */
		if (!nd && ymdp && proto.d) {
			d[nd++] = proto.d;
		}
	}
	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* use lsb of wd_mask to indicate
				 * non-0 wday counts, as in nMO,nTU, etc. */
				wd_mask |= 0b1U;
			}
		}
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U)) {
		goto fin;
	}

	/* fill up the array the hard way */
	for (res = 0UL, tries = 64U; res < nti && --tries; y += rr->inter) {
		bitint383_t cand = {0U};
		int yd;

		/* stick to note 2 on page 44, RFC 5545 */
		if (wd_mask && (nd || bi383_has_bits_p(&rr->doy))) {
			/* yd/ymd, dealt with later */
			;
		} else if (wd_mask && bi63_has_bits_p(rr->wk)) {
			/* ywd */
			fill_yly_ywd(&cand, y, rr->wk, &rr->dow);
		} else if (wd_mask && nm) {
			/* ymcw or special expand for monthly,
			 * see note 2 on page 44, RFC 5545 */
			if (wd_mask & 0b1U) {
				/* only start ymcw filling when we're sure
				 * that there are non-0 counts */
				fill_yly_ymcw(&cand, y, &rr->dow, m, nm);
			}
			fill_yly_md_all(&cand, y, m, nm, wd_mask);
		} else if (wd_mask) {
			/* ycw or special expand for yearly,
			 * see note 2 on page 44, RFC 5545 */
			if (wd_mask & 0b1U) {
				/* only start ycw filling when we're sure
				 * that there are non-0 counts */
				fill_yly_ycw(&cand, y, &rr->dow);
			}
			fill_yly_yd_all(&cand, y, wd_mask);
		}

		/* extend by yd */
		fill_yly_yd(&cand, y, &rr->doy, wd_mask);


		/* extend by ymd */
		if (UNLIKELY(!nm && !nd || bi383_has_bits_p(&rr->easter))) {
			/* don't fill up any ymds
			 * in presence of BYEASTER MDs act as a filter
			 * so extend by easter now */
			fill_yly_eastr(
				&cand, y, &rr->easter,
				rr->mon, rr->dom, wd_mask);
		} else if (!nm) {
			fill_yly_ymd_all_m(&cand, y, d, nd, wd_mask);
		} else if (!nd) {
			fill_yly_ymd_all_d(&cand, y, m, nm, wd_mask);
		} else {
			fill_yly_ymd(&cand, y, m, nm, d, nd, wd_mask);
		}

		/* limit by setpos */
		clr_poss(&cand, &rr->pos);

		/* add/subtract days */
		add_poss(&cand, y, &rr->add);

		/* now check the bitset */
		for (bitint_iter_t all = 0UL;
		     res < nti && (yd = bi383_next(&all, &cand), all);) {
			for (ENUM_INIT(e, iS, iM, iH);
			     ENUM_COND(e, iS, iM, iH);
			     ENUM_ITER(e, iS, iM, iH)) {
				echs_instant_t x = {
					.y = y,
					.m = yd / 32U + 1U,
					.d = yd % 32U,
					.H = e.H[iH],
					.M = e.M[iM],
					.S = e.S[iS],
					.ms = proto.ms,
				};

				if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
					goto fin;
				}
				if (UNLIKELY(echs_instant_lt_p(x, proto))) {
					continue;
				}
				tries = 64U;
				tgt[res++] = x;
			}
		}
	}
fin:
	return res;
}

size_t
rrul_fill_mly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	/* unrolled day bi31, we use 2 * 31 because by monthdays can
	 * also be denoted negatively, thus 1, -1, 2, -2, ..., 31, -31 is
	 * the biggest possible BYMONTHDAY value */
	int d[2U * 31U];
	size_t nd;
	size_t res = 0UL;
	size_t tries;
	uint8_t wd_mask = 0U;
	bool ymdp;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	} else if (UNLIKELY(bui31_has_bits_p(rr->mon))) {
		/* upgrade to YEARLY */
		return rrul_fill_yly(tgt, nti, rr);
	}

	/* check if we're ymd only */
	ymdp = !bi447_has_bits_p(&rr->dow) &&
		!bi31_has_bits_p(rr->dom);

	/* generate a set of minutes and seconds */
	(void)make_enum(&e, proto, rr);

	with (int tmpd) {
		nd = 0UL;
		for (bitint_iter_t di = 0U;
		     nd < nti && (tmpd = bi31_next(&di, rr->dom), di);
		     d[nd++] = tmpd);

		/* fill up with the default */
		if (!nd && ymdp && proto.d) {
			d[nd++] = proto.d;
		}
	}
	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* use lsb of wd_mask to indicate
				 * non-0 wday counts, as in nMO,nTU, etc. */
				wd_mask |= 0b1U;
			}
		}
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U)) {
		goto fin;
	}

	/* fill up the array the hard way */
	for (res = 0UL, tries = 64U; res < nti && --tries;
	     ({
		     if ((m += rr->inter) > 12U) {
			     y += m / 12U;
			     m %= 12U;
		     }
	     })) {
		bitint383_t cand = {0U};
		int yd;

		/* stick to note 1 on page 44, RFC 5545 */
		if (wd_mask && nd) {
			/* ymd, dealt with later */
			;
		} else if (wd_mask) {
			/* ycw or special expand for yearly,
			 * see note 2 on page 44, RFC 5545 */
			if (wd_mask & 0b1U) {
				/* only start ycw filling when we're sure
				 * that there are non-0 counts */
				fill_mly_ymcw(&cand, y, m, &rr->dow);
			}
			fill_mly_ymd_all_d(&cand, y, m, wd_mask);
		}

		/* extend by ymd */
		if (nd) {
			fill_mly_ymd(&cand, y, m, d, nd, wd_mask);
		}

		/* limit by setpos */
		clr_poss(&cand, &rr->pos);

		/* add/subtract days */
		add_poss(&cand, y, &rr->add);

		/* now check the bitset */
		for (bitint_iter_t all = 0UL;
		     res < nti && (yd = bi383_next(&all, &cand), all);) {
			for (ENUM_INIT(e, iS, iM, iH);
			     ENUM_COND(e, iS, iM, iH);
			     ENUM_ITER(e, iS, iM, iH)) {
				echs_instant_t x = {
					.y = y,
					.m = yd / 32U + 1U,
					.d = yd % 32U,
					.H = e.H[iH],
					.M = e.M[iM],
					.S = e.S[iS],
					.ms = proto.ms,
				};

				if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
					goto fin;
				}
				if (UNLIKELY(echs_instant_lt_p(x, proto))) {
					continue;
				}
				tries = 64U;
				tgt[res++] = x;
			}
		}
	}
fin:
	return res;
}

size_t
rrul_fill_wly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	unsigned int d = proto.d;
	unsigned int wd_mask = 0U;
	unsigned int m_mask = 0U;
	size_t res = 0UL;
	/* number of days in the current month */
	unsigned int maxd;
	/* increments induced by wd_mask */
	uint_fast32_t wd_incs = 0UL;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* generate a set of minutes and seconds */
	(void)make_enum(&e, proto, rr);

	/* set up the wday mask */
	with (unsigned int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			/* non-0 wday counts, as in nMO,nTU, etc.
			 * are illegal in the WEEKLY frequency and
			 * are ignored here */
			if (tmp >= MON && tmp <= SUN) {
				wd_mask |= (uint8_t)(1U << tmp);
			}
		}
	}

	/* set up the month mask */
	with (unsigned int tmp) {
		for (bitint_iter_t moni = 0UL;
		     (tmp = bui31_next(&moni, rr->mon), moni);) {
			m_mask |= 1U << tmp;
		}
	}

	if (!m_mask) {
		/* because we're subtractive, allow all months in the m_mask if
		 * all of the actual mask months are 0 */
		m_mask |= 0b1111111111110U;
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U || !d || d > 31U)) {
		goto fin;
	}

	if (wd_mask) {
		unsigned int w = ymd_get_wday(y, m, d);

		/* duplicate the wd_mask so we can just right shift it
		 * and wrap around the end of the week */
		wd_mask |= wd_mask << 7U;
		/* zap to current day so increments are relative to DTSTART */
		wd_mask >>= w;
		/* clamp wd_mask to exactly 7 days */
		wd_mask &= 0b111111U;
		/* calculate wd increments
		 * i.e. a bitset of increments, 4bits per increment */
		for (unsigned int i = 0U, j = 0U;
		     wd_mask; wd_mask >>= 1U, i++) {
			if (wd_mask & 0b1U) {
				wd_incs |= (i & 0b1111U) << j;
				i = 0U;
				j += 4U;
			}
		}
	}

	/* fill up the array the hard way */
	for (res = 0UL, maxd = __get_ndom(y, m); res < nti;
	     ({
		     d += rr->inter * 7U;
		     while (d > maxd) {
			     d--, d %= maxd, d++;
			     if (++m > 12U) {
				     y++;
				     m = 1U;
			     }
			     maxd = __get_ndom(y, m);
		     }
	     })) {
		uint_fast32_t incs = wd_incs;
		unsigned int this_maxd = maxd;
		unsigned int this_d = d;
		unsigned int this_m = m;
		unsigned int this_y = y;

		do {
			this_d += incs & 0b1111U;

			while (this_d > this_maxd) {
				this_d--, this_d %= this_maxd, this_d++;
				if (++this_m > 12U) {
					this_y++;
					this_m = 1U;
				}
				this_maxd = __get_ndom(this_y, this_m);
			}

			for (ENUM_INIT(e, iS, iM, iH);
			     ENUM_COND(e, iS, iM, iH);
			     ENUM_ITER(e, iS, iM, iH)) {
				echs_instant_t x = {
					.y = this_y,
					.m = this_m,
					.d = this_d,
					.H = e.H[iH],
					.M = e.M[iM],
					.S = e.S[iS],
					.ms = proto.ms,
				};

				if (UNLIKELY(echs_instant_lt_p(x, proto))) {
					continue;
				}
				if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
					goto fin;
				} else if (!(m_mask & (1U << this_m))) {
					/* skip the whole month */
					goto skip;
				}
				tgt[res++] = x;
			}
		} while ((incs >>= 4U) && res < nti);
	skip:
		;
	}

fin:
	return res;
}

size_t
rrul_fill_dly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	unsigned int d = proto.d;
	size_t res = 0UL;
	uint8_t wd_mask = 0U;
	unsigned int m_mask = 0U;
	uint_fast32_t posd_mask = 0U;
	uint_fast32_t negd_mask = 0U;
	unsigned int w;
	/* number of days in the current month */
	unsigned int maxd;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* non-0 wday counts, as in nMO,nTU, etc.
				 * are illegal in the DAILY frequency */
				wd_mask |= 0b1U;
			}
		}
	}
	if (!(wd_mask >> 1U)) {
		/* because we're subtractive, allow all days in the wd_mask if
		 * all of the actual mask days are 0 */
		wd_mask |= 0b11111110U;
	} else if (rr->inter == 1U) {
		/* aaaah, what they want in fact is a weekly schedule
		 * with the days in wd_mask */
		return rrul_fill_wly(tgt, nti, rr);
	}

	/* generate a set of hours, minutes and seconds */
	(void)make_enum(&e, proto, rr);

	/* set up the month mask */
	with (unsigned int tmp) {
		for (bitint_iter_t moni = 0UL;
		     (tmp = bui31_next(&moni, rr->mon), moni);) {
			m_mask |= 1U << tmp;
		}
	}
	if (!m_mask) {
		/* because we're subtractive, allow all months in the m_mask if
		 * all of the actual mask months are 0 */
		m_mask = 0b1111111111110U;
	}

	/* set up the days masks */
	with (int tmp) {
		for (bitint_iter_t domi = 0UL;
		     (tmp = bi31_next(&domi, rr->dom), domi);) {
			if (tmp > 0) {
				posd_mask |= 1U << tmp;
			} else if (tmp < 0) {
				negd_mask |= 1U << (unsigned int)(-++tmp);
			}
		}
	}
	if (!posd_mask && !negd_mask) {
		/* because we're subtractive, allow all doms in the d_masks if
		 * all of the actual day-of-months are 0 */
		posd_mask = 0b11111111111111111111111111111111U;
		negd_mask = 0b11111111111111111111111111111111U;
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U || !d || d > 31U)) {
		goto fin;
	}

	/* fill up the array the hard way */
	for (res = 0UL, w = ymd_get_wday(y, m, d),
		     maxd = __get_ndom(y, m);
	     res < nti;
	     ({
		     d += rr->inter;
		     w += rr->inter;
		     if (w > SUN) {
			     w = w % 7U ?: SUN;
		     }
		     while (d > maxd) {
			     d--, d %= maxd, d++;
			     if (++m > 12U) {
				     y++;
				     m = 1U;
			     }
			     maxd = __get_ndom(y, m);
		     }
	     })) {
		/* we're subtractive, so check if the current ymd matches
		 * if not, just continue and check the next candidate */
		if (!(wd_mask & (1U << w))) {
			/* huh? */
			continue;
		} else if (!(m_mask & (1U << m))) {
			/* skip the whole month */
			continue;
		} else if (!(posd_mask & (1U << d)) &&
			   !(negd_mask & (1U << (maxd - d)))) {
			/* day is filtered */
			continue;
		}

		for (ENUM_INIT(e, iS, iM, iH);
		     ENUM_COND(e, iS, iM, iH); ENUM_ITER(e, iS, iM, iH)) {
			echs_instant_t x = {
				.y = y,
				.m = m,
				.d = d,
				.H = e.H[iH],
				.M = e.M[iM],
				.S = e.S[iS],
				.ms = proto.ms,
			};
			if (UNLIKELY(echs_instant_lt_p(x, proto))) {
				continue;
			} else if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
				goto fin;
			}
			tgt[res++] = x;
		}
	}
fin:
	return res;
}

size_t
rrul_fill_Hly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	unsigned int d = proto.d;
	unsigned int H = proto.H;
	size_t res = 0UL;
	uint8_t wd_mask = 0U;
	unsigned int m_mask = 0U;
	uint_fast32_t posd_mask = 0U;
	uint_fast32_t negd_mask = 0U;
	uint_fast32_t H_mask = 0U;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ECHS_ALL_DAY */
	if (UNLIKELY(H == ECHS_ALL_DAY)) {
		H = 0U;
	}

	/* generate a set of minutes and seconds */
	(void)make_enum(&e, proto, rr);

	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* non-0 wday counts, as in nMO,nTU, etc.
				 * are illegal in the DAILY frequency */
				wd_mask |= 0b1U;
			}
		}
	}
	if (!(wd_mask >> 1U)) {
		/* because we're subtractive, allow all days in the wd_mask if
		 * all of the actual mask days are 0 */
		wd_mask |= 0b11111110U;
	}

	/* set up the month mask */
	with (unsigned int tmp) {
		for (bitint_iter_t moni = 0UL;
		     (tmp = bui31_next(&moni, rr->mon), moni);) {
			m_mask |= 1U << tmp;
		}
	}
	if (!m_mask) {
		/* because we're subtractive, allow all months in the m_mask if
		 * all of the actual mask months are 0 */
		m_mask = 0b1111111111110U;
	}

	/* set up the days masks */
	with (int tmp) {
		for (bitint_iter_t domi = 0UL;
		     (tmp = bi31_next(&domi, rr->dom), domi);) {
			if (tmp > 0) {
				posd_mask |= 1U << tmp;
			} else if (tmp < 0) {
				negd_mask |= 1U << (unsigned int)(-++tmp);
			}
		}
	}
	if (!posd_mask && !negd_mask) {
		/* because we're subtractive, allow all doms in the d_masks if
		 * all of the actual day-of-months are 0 */
		posd_mask = 0b11111111111111111111111111111111U;
		negd_mask = 0b11111111111111111111111111111111U;
	}

	/* set up the hour mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Hi = 0UL;
		     (tmp = bui31_next(&Hi, rr->H), Hi);) {
			H_mask |= 1U << tmp;
		}
	}
	if (!H_mask) {
		/* because we're limiting results, allow all hours when
		 * the H bitsets isn't set */
		H_mask = ~H_mask;
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U || !d || d > 31U)) {
		goto fin;
	}

	/* fill up the array the naive way */
	for (unsigned int w = ymd_get_wday(y, m, d), yd = ymd_get_yd(y, m, d),
		     maxd = __get_ndom(y, m), maxy = (y % 4U) ? 365 : 366;
	     res < nti;
	     ({
		     if ((H += rr->inter) >= 24U) {
			     d += H / 24U, w += H / 24U, yd += H / 24U;
			     H %= 24U;
			     if (w > SUN) {
				     w = w % 7U ?: SUN;
			     }
			     while (d > maxd) {
				     d--, d %= maxd, d++;
				     if (++m > 12U) {
					     y++;
					     m = 1U;
					     yd -= maxy - 1;
					     maxy = (y % 4U) ? 365 : 366;
				     }
				     maxd = __get_ndom(y, m);
			     }
		     }
	     })) {
		/* we're subtractive, so check if the current ymd matches
		 * if not, just continue and check the next candidate */
		if (!(wd_mask & (1U << w))) {
			/* huh? */
			continue;
		} else if (!(m_mask & (1U << m))) {
			/* skip the whole month */
			continue;
		} else if (!(posd_mask & (1U << d)) &&
			   !(negd_mask & (1U << (maxd - d)))) {
			/* day is filtered */
			continue;
		} else if (!(H_mask & (1U << H))) {
			/* hour is filtered */
			continue;
		} else if (bi383_has_bits_p(&rr->doy)) {
			/* iterate manually */
			int tmp;
			for (bitint_iter_t doyi = 0UL;
			     (tmp = bi383_next(&doyi, &rr->doy), doyi);) {
				if (tmp > 0 && (unsigned int)tmp == yd ||
				    tmp < 0 && maxy - ++tmp == yd) {
					/* that's clearly a match */
					goto bang;
				}
			}
			continue;
		}

	bang:
		for (ENUM_INIT(e, iS, iM);
		     ENUM_COND(e, iS, iM); ENUM_ITER(e, iS, iM)) {
			echs_instant_t x = {
				.y = y,
				.m = m,
				.d = d,
				.H = H,
				.M = e.M[iM],
				.S = e.S[iS],
				.ms = proto.ms,
			};

			if (UNLIKELY(echs_instant_lt_p(x, proto))) {
				continue;
			} else if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
				goto fin;
			}
			tgt[res++] = x;
		}
	}
fin:
	return res;
}

size_t
rrul_fill_Mly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	unsigned int d = proto.d;
	unsigned int H = proto.H;
	unsigned int M = proto.M;
	size_t res = 0UL;
	uint8_t wd_mask = 0U;
	unsigned int m_mask = 0U;
	uint_fast32_t posd_mask = 0U;
	uint_fast32_t negd_mask = 0U;
	uint_fast32_t H_mask = 0U;
	uint_fast64_t M_mask = 0U;
	struct enum_s e;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ECHS_ALL_DAY */
	if (UNLIKELY(H == ECHS_ALL_DAY)) {
		H = 0U;
		M = 0U;
	}

	/* generate a set of minutes and seconds */
	(void)make_enum(&e, proto, rr);

	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* non-0 wday counts, as in nMO,nTU, etc.
				 * are illegal in the DAILY frequency */
				wd_mask |= 0b1U;
			}
		}
	}
	if (!(wd_mask >> 1U)) {
		/* because we're subtractive, allow all days in the wd_mask if
		 * all of the actual mask days are 0 */
		wd_mask |= 0b11111110U;
	}

	/* set up the month mask */
	with (unsigned int tmp) {
		for (bitint_iter_t moni = 0UL;
		     (tmp = bui31_next(&moni, rr->mon), moni);) {
			m_mask |= 1U << tmp;
		}
	}
	if (!m_mask) {
		/* because we're subtractive, allow all months in the m_mask if
		 * all of the actual mask months are 0 */
		m_mask = 0b1111111111110U;
	}

	/* set up the days masks */
	with (int tmp) {
		for (bitint_iter_t domi = 0UL;
		     (tmp = bi31_next(&domi, rr->dom), domi);) {
			if (tmp > 0) {
				posd_mask |= 1U << tmp;
			} else if (tmp < 0) {
				negd_mask |= 1U << (unsigned int)(-++tmp);
			}
		}
	}
	if (!posd_mask && !negd_mask) {
		/* because we're subtractive, allow all doms in the d_masks if
		 * all of the actual day-of-months are 0 */
		posd_mask = 0b11111111111111111111111111111111U;
		negd_mask = 0b11111111111111111111111111111111U;
	}

	/* set up the hour mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Hi = 0UL;
		     (tmp = bui31_next(&Hi, rr->H), Hi);) {
			H_mask |= 1U << tmp;
		}
	}
	if (!H_mask) {
		/* because we're limiting results, allow all hours when
		 * the H bitsets isn't set */
		H_mask = ~H_mask;
	}

	/* set up the minute mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Mi = 0UL;
		     (tmp = bui63_next(&Mi, rr->M), Mi);) {
			M_mask |= 1ULL << tmp;
		}
	}
	if (!M_mask) {
		/* because we're limiting results, allow all minutes when
		 * the M bits aren't set */
		M_mask = ~M_mask;
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U || !d || d > 31U)) {
		goto fin;
	}

	/* fill up the array the naive way */
	for (unsigned int w = ymd_get_wday(y, m, d), maxd = __get_ndom(y, m);
	     res < nti;
	     ({
		     if ((M += rr->inter) >= 60U) {
			     H += M / 60U, M %= 60U;
			     if (H >= 24U) {
				     d += H / 24U, w += H / 24U, H %= 24U;
				     if (w > SUN) {
					     w = w % 7U ?: SUN;
				     }
				     while (d > maxd) {
					     d--, d %= maxd, d++;
					     if (++m > 12U) {
						     y++;
						     m = 1U;
					     }
					     maxd = __get_ndom(y, m);
				     }
			     }
		     }
	     })) {
		/* we're subtractive, so check if the current ymd matches
		 * if not, just continue and check the next candidate */
		if (!(wd_mask & (1U << w))) {
			/* huh? */
			continue;
		} else if (!(m_mask & (1U << m))) {
			/* skip the whole month */
			continue;
		} else if (!(posd_mask & (1U << d)) &&
			   !(negd_mask & (1U << (maxd - d)))) {
			/* day is filtered */
			continue;
		} else if (!(H_mask & (1U << H))) {
			/* hour is filtered */
			continue;
		} else if (!(M_mask & (1ULL << M))) {
			/* minute is filtered */
			continue;
		}

		for (ENUM_INIT(e, iS); ENUM_COND(e, iS); ENUM_ITER(e, iS)) {
			echs_instant_t x = {
				.y = y,
				.m = m,
				.d = d,
				.H = H,
				.M = M,
				.S = e.S[iS],
				.ms = proto.ms,
			};

			if (UNLIKELY(echs_instant_lt_p(x, proto))) {
				continue;
			} else if (UNLIKELY(echs_instant_lt_p(rr->until, x))) {
				goto fin;
			}
			tgt[res++] = x;
		}
	}
fin:
	return res;
}

size_t
rrul_fill_Sly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	const echs_instant_t proto = *tgt;
	unsigned int y = proto.y;
	unsigned int m = proto.m;
	unsigned int d = proto.d;
	unsigned int H = proto.H;
	unsigned int M = proto.M;
	unsigned int S = proto.S;
	size_t res = 0UL;
	uint8_t wd_mask = 0U;
	unsigned int m_mask = 0U;
	uint_fast32_t posd_mask = 0U;
	uint_fast32_t negd_mask = 0U;
	uint_fast32_t H_mask = 0U;
	uint_fast64_t M_mask = 0U;
	uint_fast64_t S_mask = 0U;

	if (UNLIKELY((unsigned int)rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ECHS_ALL_DAY */
	if (UNLIKELY(H == ECHS_ALL_DAY)) {
		H = 0U;
		M = 0U;
		S = 0U;
	}

	/* set up the wday mask */
	with (int tmp) {
		for (bitint_iter_t dowi = 0UL;
		     (tmp = bi447_next(&dowi, &rr->dow), dowi);) {
			if (tmp >= (int)MON && tmp <= (int)SUN) {
				wd_mask |= (uint8_t)(1U << (unsigned int)tmp);
			} else {
				/* non-0 wday counts, as in nMO,nTU, etc.
				 * are illegal in the DAILY frequency */
				wd_mask |= 0b1U;
			}
		}
	}
	if (!(wd_mask >> 1U)) {
		/* because we're subtractive, allow all days in the wd_mask if
		 * all of the actual mask days are 0 */
		wd_mask |= 0b11111110U;
	}

	/* set up the month mask */
	with (unsigned int tmp) {
		for (bitint_iter_t moni = 0UL;
		     (tmp = bui31_next(&moni, rr->mon), moni);) {
			m_mask |= 1U << tmp;
		}
	}
	if (!m_mask) {
		/* because we're subtractive, allow all months in the m_mask if
		 * all of the actual mask months are 0 */
		m_mask = 0b1111111111110U;
	}

	/* set up the days masks */
	with (int tmp) {
		for (bitint_iter_t domi = 0UL;
		     (tmp = bi31_next(&domi, rr->dom), domi);) {
			if (tmp > 0) {
				posd_mask |= 1U << tmp;
			} else if (tmp < 0) {
				negd_mask |= 1U << (unsigned int)(-++tmp);
			}
		}
	}
	if (!posd_mask && !negd_mask) {
		/* because we're subtractive, allow all doms in the d_masks if
		 * all of the actual day-of-months are 0 */
		posd_mask = 0b11111111111111111111111111111111U;
		negd_mask = 0b11111111111111111111111111111111U;
	}

	/* set up the hour mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Hi = 0UL;
		     (tmp = bui31_next(&Hi, rr->H), Hi);) {
			H_mask |= 1U << tmp;
		}
	}
	if (!H_mask) {
		/* because we're limiting results, allow all hours when
		 * the H bitsets isn't set */
		H_mask = ~H_mask;
	}

	/* set up the minute mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Mi = 0UL;
		     (tmp = bui63_next(&Mi, rr->M), Mi);) {
			M_mask |= 1ULL << tmp;
		}
	}
	if (!M_mask) {
		/* because we're limiting results, allow all minutes when
		 * the M bits aren't set */
		M_mask = ~M_mask;
	}

	/* set up the second mask */
	with (unsigned int tmp) {
		for (bitint_iter_t Si = 0UL;
		     (tmp = bui63_next(&Si, rr->S), Si);) {
			S_mask |= 1ULL << tmp;
		}
	}
	if (!S_mask) {
		/* because we're limiting results, allow all seconds when
		 * the S bits aren't set */
		S_mask = ~S_mask;
	}

	/* check ranges before filling */
	if (UNLIKELY(y < 1600U || !m || m > 12U || !d || d > 31U)) {
		goto fin;
	}

	/* fill up the array the naive way */
	for (unsigned int w = ymd_get_wday(y, m, d), maxd = __get_ndom(y, m);
	     res < nti;
	     ({
		     if ((S += rr->inter) >= 60U) {
			     M += S / 60U, S %= 60U;
			     if (M >= 60U) {
				     H += M / 60U, M %= 60U;
				     if (H >= 24U) {
					     d += H / 24U, w += H / 24U;
					     H %= 24U;
					     if (w > SUN) {
						     w = w % 7U ?: SUN;
					     }
					     while (d > maxd) {
						     d--, d %= maxd, d++;
						     if (++m > 12U) {
							     y++;
							     m = 1U;
						     }
						     maxd = __get_ndom(y, m);
					     }
				     }
			     }
		     }
	     })) {
		/* we're subtractive, so check if the current ymd matches
		 * if not, just continue and check the next candidate */
		if (!(wd_mask & (1U << w))) {
			/* huh? */
			continue;
		} else if (!(m_mask & (1U << m))) {
			/* skip the whole month */
			continue;
		} else if (!(posd_mask & (1U << d)) &&
			   !(negd_mask & (1U << (maxd - d)))) {
			/* day is filtered */
			continue;
		} else if (!(H_mask & (1U << H))) {
			/* hour is filtered */
			continue;
		} else if (!(M_mask & (1ULL << M))) {
			/* minute is filtered */
			continue;
		} else if (!(S_mask & (1ULL << S))) {
			/* second is filtered */
			continue;
		}

		tgt[res].y = y;
		tgt[res].m = m;
		tgt[res].d = d;
		tgt[res].H = H;
		tgt[res].M = M;
		tgt[res].S = S;
		if (UNLIKELY(echs_instant_lt_p(rr->until, tgt[res]))) {
			goto fin;
		}
		res++;
	}
fin:
	return res;
}


/* rrules as query language */
bool
echs_instant_matches_p(rrulsp_t filt, echs_instant_t inst)
{
	/* whitelisted events and state */
	static echs_instant_t wl[256U];
	static size_t nwl;
	static size_t iwl;

	if (UNLIKELY(nwl > countof(wl))) {
		goto never;
	}
ffw:
	/* fast-forward whitelist to within the range of INST */
	for (; iwl < nwl && echs_instant_lt_p(wl[iwl], inst); iwl++);
	if (UNLIKELY(iwl >= nwl)) {
		/* refill filter list, start out with I */
		echs_instant_t proto = inst;

		proto.d = 0;
		for (size_t i = 0UL; i < countof(wl); i++) {
			wl[i] = proto;
		}
		if (UNLIKELY(!(nwl = rrul_fill_yly(wl, countof(wl), filt)))) {
			nwl = -1UL;
			goto never;
		}
		/* reset state */
		iwl = 0UL;
		/* retry the fast forwarding */
		goto ffw;
	}
	/* now either wl[iwl] == inst ... */
	if (echs_instant_eq_p(wl[iwl], inst)) {
		return true;
	}
never:
	/* ... or we're past it */
	return false;
}

/* evrrul.c ends here */
