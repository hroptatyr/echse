/*** dt-strpf.c -- parser and formatter funs for echse
 *
 * Copyright (C) 2011-2020 Sebastian Freundt
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
 **/
/* implementation part of date-core-strpf.h */
#if !defined INCLUDED_dt_strpf_c_
#define INCLUDED_dt_strpf_c_

#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdint.h>
#include "dt-strpf.h"
#include "nifty.h"

#define HOURS_PER_DAY	(24U)
#define MINS_PER_HOUR	(60U)
#define SECS_PER_MIN	(60U)
#define MSECS_PER_SEC	(1000U)
#define SECS_PER_DAY	(HOURS_PER_DAY * MINS_PER_HOUR * SECS_PER_MIN)
#define MSECS_PER_DAY	(SECS_PER_DAY * MSECS_PER_SEC)
#define DAYS_PER_WEEK	(7U)
#define DAYS_PER_YEAR	(365U)

static __attribute__((const, pure)) unsigned int
ilog2_ceil(unsigned int n)
{
/* return the next 2-power > N, at least 16 though */
	static const uint8_t de_bruijns[] = {
		1U, 10U, 2U, 11U, 14U, 22U, 3U, 30U,
		12U, 15U, 17U, 19U, 23U, 26U, 4U, 31U,
		9U, 13U, 21U, 29U, 16U, 18U, 25U, 8U,
		20U, 28U, 24U, 7U, 27U, 6U, 5U, 32U,
	};
	n |= 0b1111U;
	n |= n >> 1U;
	n |= n >> 2U;
	n |= n >> 4U;
	n |= n >> 8U;
	n |= n >> 16U;
	return de_bruijns[(uint32_t)(n * 0x07c4acddu) >> 27U];
}

static __attribute__((const, pure)) unsigned int
ilog10_ceil(unsigned int n)
{
	static unsigned int const _10_pows[] = {
		1U, 10U, 100U, 1000U, 10000U, 100000U,
		1000000U, 10000000U, 100000000U, 1000000000U,
	};
	unsigned int l2 = ilog2_ceil(n);

	/* get estimate for 10-power */
	l2 = l2 * 1233U >> 12U;
	return l2 + (n >= _10_pows[l2]);
}


static size_t
ui32tpstr(char *restrict buf, size_t bsz, uint32_t d, int pad)
{
/* all strings should be little */
#define C(x)	(char)((x) % 10U ^ '0'); x /= 10U
	size_t res;
	size_t i;

	if (UNLIKELY(d > 1000000000)) {
		return 0U;
	} else if ((res = (size_t)pad) > bsz) {
		return 0U;
	}
	switch ((i = res)) {
	case 9U:
		/* for nanoseconds */
		buf[--i] = C(d);
		buf[--i] = C(d);
		buf[--i] = C(d);
		/*@fallthrough@*/
	case 6U:
		/* for microseconds */
		buf[--i] = C(d);
		buf[--i] = C(d);
		/*@fallthrough@*/
	case 4U:
		/* for western year numbers */
		buf[--i] = C(d);
		/*@fallthrough@*/
	case 3U:
		/* for milliseconds or doy et al. numbers */
		buf[--i] = C(d);
		/*@fallthrough@*/
	case 2U:
		/* hours, mins, secs, doms, moys, etc. */
		buf[--i] = C(d);
		/*@fallthrough@*/
	case 1U:
		buf[--i] = C(d);
		break;
	default:
	case 0:
		res = 0U;
		break;
	}
	return res;
}

static size_t
ui32tostr(char *restrict buf, size_t bsz, uint32_t d)
{
	unsigned int l = ilog10_ceil(d);
	unsigned int i;

	if (UNLIKELY(l >= bsz)) {
		return 0U;
	}
	switch ((i = l)) {
	case 10U:
		buf[--i] = C(d);
	case 9U:
		buf[--i] = C(d);
	case 8U:
		buf[--i] = C(d);
	case 7U:
		buf[--i] = C(d);
	case 6U:
		buf[--i] = C(d);
	case 5U:
		buf[--i] = C(d);
	case 4U:
		buf[--i] = C(d);
	case 3U:
		buf[--i] = C(d);
	case 2U:
		buf[--i] = C(d);
	case 1U:
		buf[--i] = C(d);
		break;
	case 0U:
	default:
		break;
	}
	return l;
#undef C
}


echs_instant_t
dt_strp(const char *str, char **on, size_t len)
{
/* code dupe, see __strpd_std() */
	echs_instant_t res = {.u = 0U};
	unsigned int tmp = 0U;
	const char *restrict sp = str;
	const char *const ep = str + (len ?: 23U);

	if (UNLIKELY(sp == NULL)) {
		goto nul;
	} else if (UNLIKELY(len && len < 8U)) {
		goto nul;
	}
	/* read the year */
	tmp = 0U;
	if ((uint8_t)(*sp ^ '0') < 10U) {
		tmp *= 10U;
		tmp += (uint8_t)(*sp++ ^ '0');
		if ((uint8_t)(*sp ^ '0') < 10U) {
			tmp *= 10U;
			tmp += (uint8_t)(*sp++ ^ '0');
			if ((uint8_t)(*sp ^ '0') < 10U) {
				tmp *= 10U;
				tmp += (uint8_t)(*sp++ ^ '0');
				if ((uint8_t)(*sp ^ '0') < 10U) {
					tmp *= 10U;
					tmp += (uint8_t)(*sp++ ^ '0');
				} else {
					goto nul;
				}
			} else {
				goto nul;
			}
		} else {
			goto nul;
		}
	} else {
		goto nul;
	}
	/* year can be set now */
	res.y = tmp;

	/* advance over ISO-8601 dash */
	if (*sp == '-') {
		sp++;
	}

	/* snarf month */
	switch (*sp++) {
	case '0':
		if ((uint8_t)(*sp ^ '0') < 10U) {
			tmp = *sp++ ^ '0';
			break;
		}
		goto nul;
	case '1':
		if ((uint8_t)(*sp ^ '0') < 10U) {
			tmp = 10U + (*sp++ ^ '0');
			break;
		}
		goto nul;
	default:
		goto nul;
	}
	/* that's the month gone */
	res.m = tmp;

	/* again, advance over ISO-8601 separator */
	if (*sp == '-') {
		sp++;
	}
	if (UNLIKELY(sp >= ep)) {
		goto nul;
	}

	/* snarf mday, ... if there's a separator */
	switch (*sp++) {
	case '0':
		tmp = 0U;
		break;
	case '1':
		tmp = 10U;
		break;
	case '2':
		tmp = 20U;
		break;
	case '3':
		tmp = 30U;
		break;
	default:
		goto nul;
	}
	if (UNLIKELY(sp >= ep)) {
		goto nul;
	} else if ((uint8_t)(*sp ^ '0') < 10U) {
		tmp += *sp++ ^ '0';
		res.d = tmp;
	} else {
		goto nul;
	}

	if (sp >= ep || *sp != 'T' && *sp != ' ') {
		res.H = ECHS_ALL_DAY;
		goto res;
	} else if (UNLIKELY(sp++ + 4U >= ep)) {
		goto nul;
	}

	/* and now parse the time */
	tmp = 0U;
	switch (*sp++) {
	case '2':
		tmp = 20U;
		if ((uint8_t)(*sp ^ '0') < 4U) {
			tmp += *sp++ ^ '0';
			break;
		}
		goto nul;
	case '1':
		tmp = 10U;
		/*@fallthrough@*/
	case '0':
		if ((uint8_t)(*sp ^ '0') < 10U) {
			tmp += *sp++ ^ '0';
			break;
		}
		goto nul;
	default:
		/* rubbish */
		goto nul;
	}
	/* big success */
	res.H = tmp;

	/* furthermore, we demand minutes */
	if (*sp == ':') {
		sp++;
	}
	if (UNLIKELY(sp + 2U > ep)) {
		goto nul;
	}
	if ((uint8_t)(*sp ^ '0') < 6U &&
	    (tmp = 10U * (*sp++ ^ '0'), (uint8_t)(*sp ^ '0') < 10U)) {
		tmp += *sp++ ^ '0';
	} else {
		goto nul;
	}
	res.M = tmp;

	/* seconds are optional */
	if (sp >= ep || *sp == ':') {
		sp++;
	}
	if (UNLIKELY(sp + 2U > ep)) {
		goto res;
	}
	if (sp[0U] == '6' && sp[1U] == '0') {
		tmp = 60U;
		sp += 2U;
	} else if ((uint8_t)(*sp ^ '0') < 6U &&
		   (tmp = 10U * (*sp++ ^ '0'), (uint8_t)(*sp ^ '0') < 10U)) {
		tmp += *sp++ ^ '0';
	} else {
		tmp = 0U;
	}
	res.S = tmp;

	if (sp >= ep || *sp != '.') {
		res.ms = ECHS_ALL_SEC;
		goto res;
	}

	/* millisecond part */
	for (tmp = 100U; (uint8_t)(*++sp ^ '0') < 10U && tmp < 100000U;) {
		tmp *= 10U;
		tmp += *sp ^ '0';
	}
	res.ms = tmp % 1000U;
res:
	if (on != NULL) {
		/* always overread final Z */
		if (LIKELY(*sp == 'Z')) {
			sp++;
		}
		*on = deconst(sp);
	}
	return res;
nul:
	return (echs_instant_t){.u = 0U};
}

size_t
dt_strf(char *restrict buf, size_t bsz, echs_instant_t inst)
{
	char *restrict bp = buf;
#define bz	(bsz - (bp - buf))

	bp += ui32tpstr(bp, bz, inst.y, 4);
	*bp++ = '-';
	bp += ui32tpstr(bp, bz, inst.m, 2);
	*bp++ = '-';
	bp += ui32tpstr(bp, bz, inst.d, 2);

	if (LIKELY(!echs_instant_all_day_p(inst))) {
		*bp++ = 'T';
		bp += ui32tpstr(bp, bz, inst.H, 2);
		*bp++ = ':';
		bp += ui32tpstr(bp, bz, inst.M, 2);
		*bp++ = ':';
		bp += ui32tpstr(bp, bz, inst.S, 2);
		if (LIKELY(!echs_instant_all_sec_p(inst))) {
			*bp++ = '.';
			bp += ui32tpstr(bp, bz, inst.ms, 3);
		}
	}
	*bp = '\0';
	return bp - buf;
}

size_t
dt_strf_ical(char *restrict buf, size_t bsz, echs_instant_t inst)
{
	char *restrict bp = buf;
#define bz	(bsz - (bp - buf))

	bp += ui32tpstr(bp, bz, inst.y, 4);
	bp += ui32tpstr(bp, bz, inst.m, 2);
	bp += ui32tpstr(bp, bz, inst.d, 2);

	if (LIKELY(!echs_instant_all_day_p(inst))) {
		*bp++ = 'T';
		bp += ui32tpstr(bp, bz, inst.H, 2);
		bp += ui32tpstr(bp, bz, inst.M, 2);
		bp += ui32tpstr(bp, bz, inst.S, 2);
		*bp++ = 'Z';
	}
	*bp = '\0';
	return bp - buf;
#undef bz
}


echs_idiff_t
idiff_strp(const char *str, char **on, size_t len)
{
/* Format Definition:  This value type is defined by the following
 * notation:
 *
 * dur-value  = (["+"] / "-") "P" (dur-date / dur-time / dur-week)
 *
 * dur-date   = dur-day [dur-time]
 * dur-time   = "T" (dur-hour / dur-minute / dur-second)
 * dur-week   = 1*DIGIT "W"
 * dur-hour   = 1*DIGIT "H" [dur-minute]
 * dur-minute = 1*DIGIT "M" [dur-second]
 * dur-second = 1*DIGIT "S"
 * dur-day    = 1*DIGIT "D"
 */
	size_t i = 0U;
	echs_idiff_t res = {0};
	int dd = 0, msd = 0;
	bool negp = false;
	bool seen_D_p = false;
	bool seen_W_p = false;
	/* a special stepping to combine 'H'-seen, 'M'-seen, 'S'-seen
	 * based on their ASCII values, 0x1, 0x11, 9x111 */
	uint8_t step = 0U;
	unsigned int val;

	if (UNLIKELY(len < 3U)) {
		goto out;
	}
	switch (str[i++]) {
	case 'P':
		break;
	case '-':
		negp = true;
		/*@fallthrough@*/
	case '+':
		switch (str[i++]) {
		case 'P':
			break;
		default:
			/* nope */
			goto out;
		}
	default:
		goto out;
	}

more_date:
	val = 0U;
	for (; i < len; i++) {
		unsigned int tmp;

		if ((tmp = str[i] ^ '0') >= 10U) {
			break;
		}
		/* add next 10-power */
		val *= 10U;
		val += tmp;
	}
	switch (str[i++]) {
	case 'T':
		/* switch to time snarfing */
		break;
	case 'W':
		if (UNLIKELY(seen_W_p)) {
			res = (echs_idiff_t){0};
			goto out;
		}
		seen_W_p = true;
		dd += val * 7U;
		goto more_date;
	case 'D':
		if (UNLIKELY(seen_D_p)) {
			res = (echs_idiff_t){0};
			goto out;
		}
		seen_D_p = true;
		dd += val;
		goto more_date;
	default:
		goto out;
	}

more_time:
	val = 0U;
	for (; i < len; i++) {
		unsigned int tmp;

		if ((tmp = str[i] ^ '0') >= 10U) {
			break;
		}
		/* add next 10-power */
		val *= 10U;
		val += tmp;
	}
	/* here we just deflect the true character by some bits
	 * made up from STEP because 'M' | 0x1U == 'M', same for 'S'
	 * and 'S' | 0x11U == 'S' */
	switch (str[i++] | step) {
	case 'H':
		step |= 0x1U;
		msd += val * 60U * 60U * 1000U;
		goto more_time;
	case 'M':
		step |= 0x11U;
		msd += val * 60U * 1000U;
		goto more_time;
	case 'S':
		step |= 0x21U;
		msd += val * 1000U;
		goto more_time;
	default:
		goto out;
	}

out:
	/* make up the idiff object */
	res.d = dd * MSECS_PER_DAY + msd;
	/* check for negativity, in reality we don't want negative durations
	 * in our problem domain, ever.  they make no sense */
	if (negp) {
		/* keep a positive rest and negate the dd field */
		res.d = -res.d;
	}

	if (on != NULL) {
		*on = deconst(str + i);
	}
	return res;
}

size_t
idiff_strf(char *restrict buf, size_t bsz, echs_idiff_t idiff)
{
	unsigned int tmp;
	size_t i = 0U;

	if (UNLIKELY(bsz < 4U)) {
		return 0U;
	}
	if (UNLIKELY(idiff.d < 0)) {
		buf[i++] = '-';
		idiff.d = -idiff.d;
	}
	buf[i++] = 'P';
	if (echs_nul_idiff_p(idiff)) {
		buf[i++] = '0';
		buf[i++] = 'D';
		goto out;
	}
	if ((tmp = idiff.d / MSECS_PER_DAY)) {
		idiff.d %= MSECS_PER_DAY;
		i += ui32tostr(buf + i, bsz - i, tmp);
		if (i >= bsz) {
			goto out;
		}
		buf[i++] = 'D';
	}
	if (idiff.d && i < bsz) {
		buf[i++] = 'T';

		tmp = idiff.d / (60U * 60U * 1000U);
		idiff.d %= 60U * 60U * 1000U;
		if (tmp) {
			i += ui32tostr(buf + i, bsz - i, tmp);
			if (i >= bsz) {
				goto out;
			}
			buf[i++] = 'H';
		}

		tmp = idiff.d / (60U * 1000U);
		idiff.d %= 60U * 1000U;
		if (tmp) {
			i += ui32tostr(buf + i, bsz - i, tmp);
			if (i >= bsz) {
				goto out;
			}
			buf[i++] = 'M';
		}

		tmp = idiff.d / 1000U;
		idiff.d %= 1000U;
		if (tmp) {
			i += ui32tostr(buf + i, bsz - i, tmp);
			if (i >= bsz) {
				goto out;
			}
			buf[i++] = 'S';
		}
	}

out:
	if (LIKELY(i < bsz)) {
		buf[i] = '\0';
	} else {
		buf[--i] = '\0';
	}
	return i;
}


echs_range_t
range_strp(const char *str, char **on, size_t len)
{
	char *op = deconst(str);
	echs_range_t r;

	if (UNLIKELY(!len)) {
		goto err;
	} else if (*str == '-') {
		/* ah, lower bound seems to be -infty */
		r.beg = echs_min_instant();
	} else {
		r.beg = dt_strp(str, &op, len);
	}
	switch (*op++) {
	case '/':
		/* just a normal range then, innit? */
		r.end = dt_strp(op, on, len - (op - str));
		break;
	case '+':
		r.end = echs_max_instant();
		if (on != NULL) {
			*on = op;
		}
		break;
	default:
		/* huh? */
		goto err;
	}
	return r;
err:
	return echs_max_range();
}

size_t
range_strf(char *restrict str, size_t ssz, echs_range_t r)
{
	size_t n = 0U;

	if (UNLIKELY(!ssz)) {
		return 0U;
	} else if (UNLIKELY(ssz < 2U)) {
		*str = '\0';
		return 0U;
	} else if (UNLIKELY(echs_max_range_p(r))) {
		str[n++] = '*';
		goto fin;
	} else if (!echs_min_instant_p(r.beg)) {
		n += dt_strf(str, ssz, r.beg);
	}
	if (n < ssz && !echs_max_instant_p(r.end)) {
		str[n++] = '/';
		n += dt_strf(str + n, ssz - n, r.end);
	} else if (n < ssz) {
		str[n++] = '+';
	}
fin:
	if (LIKELY(n < ssz)) {
		str[n] = '\0';
	} else {
		str[ssz - 1U] = '\0';
	}
	return n;
}

#endif	/* INCLUDED_dt_strpf_c_ */
