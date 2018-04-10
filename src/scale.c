/*** scale.c -- calendar scales
 *
 * Copyright (C) 2016-2018 Sebastian Freundt
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "scale.h"
#include "tzob.h"
#include "nifty.h"

/* hijri calendar */
#include "dat_ummulqura.c"
#include "dat_diyanet.c"

typedef unsigned int mjd_t;

struct ymd_s {
	unsigned int y;
	unsigned int m;
	unsigned int d;
};

typedef enum {
	TYP_I,
	TYP_II,
	TYP_III,
	TYP_IV,
} hij_typ_t;

typedef enum {
	EPO_ASTRO,
	EPO_CIVIL,
} hij_epo_t;

/* for table based hijri calendars */
/* monthly transitions for calendars with fixed number of months in year */
#define SM(x)	(x[0U])
/* number of months in a year */
#define EM(x)	(x[1U])
/* the actual month transitions data */
#define MT(x)	(x + 2U)
/* determine number of months in the MT array */
#define NM(x)	(sizeof(x) / sizeof(unsigned int) - 2U)

#define SCAL2TYP(x)	(hij_typ_t)(((x) - 1U) / 2U)
#define SCAL2EPO(x)	(hij_epo_t)(((x) - 1U) % 2U)


/* shifts based on type */
static const unsigned int tsh[] = {
	[TYP_I] = 401U,
	[TYP_II] = 301U,
	[TYP_III] = 1U,
	[TYP_IV] = -199U,
};

/* epochs in julian days */
static const unsigned int epo[] = {
	[EPO_CIVIL] = 1948084U,
	[EPO_ASTRO] = 1948085U,
};

static inline __attribute__((const, pure)) mjd_t
ht2mjd(const unsigned int *cal, size_t nm, struct ymd_s h)
{
	const unsigned int i = (h.y - 1U) * 12U + (h.m - 1U) - SM(cal);

	if (UNLIKELY(i >= nm)) {
		return 0U;
	}
	return MT(cal)[i] + (h.d - 1U);
}

static inline __attribute__((const, pure)) mjd_t
hij2mjd(hij_typ_t t, hij_epo_t e, struct ymd_s h)
{
	static const unsigned int m[] = {
		0U, 0U, 30U, 59U, 89U, 118U, 148U, 177U, 207U, 236U, 266U, 295U, 325U
	};
	const unsigned int doy = m[h.m] + h.d;
	const unsigned int cyc = h.y / 30U;
	const unsigned int k = h.y % 30U;
	const unsigned int z1 = cyc * 10631U + (k * 1063100U + tsh[t]) / 3000U + doy;
	return z1 + epo[e] - 2400000U;
}

static inline __attribute__((const, pure)) mjd_t
g2mjd(struct ymd_s g)
{
	const unsigned int b = g.y + 100100U + ((int)g.m - 8) / 6;
	unsigned int d = (b * 1461U) / 4U;

	d += (153U * ((g.m + 9U) % 12U) + 2U) / 5U + g.d;
	return d - ((b / 100U) * 3U) / 4U + 752U - 34840408U - 2400000U;
}

static __attribute__((const, pure)) struct ymd_s
mjd2g(mjd_t d)
{
/* turn julian day number into gregorian date */
	unsigned int j;
	unsigned int i;
	unsigned int gd, gm, gy;

	/* this one uses julian day numbers */
	d += 2400000U;
	j = 4U * d + 139361631U;
	j += ((((4U * d + 183187720U) / 146097U) * 3U) / 4U) * 4U - 3908U;
	i = ((j % 1461U) / 4U) * 5U + 308U;
	gd = ((i % 153U) / 5U) + 1U;
	gm = ((i / 153U) % 12U) + 1U;
	gy = j / 1461U - 100100U + (int)(8 - gm) / 6;
	return (struct ymd_s){gy, gm, gd};
}

static __attribute__((const, pure)) struct ymd_s
mjd2ht(const unsigned int *cal, size_t nm, mjd_t d)
{
/* turn julian day number into hijri date,
 * this one does a scan over the month transitions array, so use seldom */
	size_t i;
	unsigned int m;

	for (i = 0U; i < nm && MT(cal)[i] <= d; i++);
	if (UNLIKELY(i >= nm)) {
		/* that's beyond our time */
		goto nil;
	}
	/* M is the month count */
	m = i + SM(cal);

	return (struct ymd_s){
		.y = (m - 1U) / 12U + 1U,
		.m = (m - 1U) % 12U + 1U,
		.d = d - MT(cal)[i - 1U] + 1U
	};
nil:
	return (struct ymd_s){};
}

static inline __attribute__((const, pure)) struct ymd_s
mjd2hij(hij_typ_t t, hij_epo_t e, mjd_t j)
{
/* integer only version of Gent's converter */
	const unsigned int z = j + 2400000U - epo[e];
	const unsigned int cyc = z / 10631U;
	const unsigned int z1 = z % 10631U;
	const unsigned int k = (3000U * z1 - tsh[t]) / 1063100U - !z1;
	const unsigned int z2 = z1 - (((int)k * 1063100 + tsh[t]) / 3000) + !z1;
	/* output */
	const unsigned int y = 30U * cyc + k;
	const unsigned int m = (10000U * z2 + 285001U) / 295000U;
	const unsigned int d = z2 - (295001 * m - 290000U) / 10000U;
	return (struct ymd_s){y, m, d};
}

static __attribute__((pure, const)) unsigned int
__ndim_ht(const unsigned int *cal, size_t nm, unsigned int y, unsigned int m)
{
/* return the number of days in (hijri) month M in (hijri) year Y. */
	const unsigned int i = (y - 1U) * 12U + (m - 1U) - SM(cal);

	if (UNLIKELY(i + 1U >= nm)) {
		return 0U;
	}
	return MT(cal)[i + 1U] - MT(cal)[i + 0U];
}

static __attribute__((const, pure)) inline unsigned int
__ndim_greg(unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y. */
	static const unsigned int mdays[] = {
		0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
	};
	unsigned int res = mdays[m];

	if (UNLIKELY(!(y % 4U) && m == 2U)) {
		res++;
	}
	return res;
}

static __attribute__((const, pure)) inline bool
__hij_inty_p(hij_typ_t t, hij_epo_t UNUSED(e), unsigned int y)
{
/* do a trial conversion to mjd and back, see whether we end up with Dhu 30
 * type I:   2, 5, 7, 10, 13, 15, 18, 21, 24, 26 & 29 as intercalary years
 * type II:  2, 5, 7, 10, 13, 16, 18, 21, 24, 26 & 29 as intercalary years
 * type III: 2, 5, 8, 10, 13, 16, 19, 21, 24, 27 & 29 as intercalary years
 * type IV:  2, 5, 8, 11, 13, 16, 19, 21, 24, 27 & 30 as intercalary years */
	const unsigned int k = y % 30U;
	const unsigned int z1 = ((k * 1063100U + tsh[t]) / 3000U + 355U) % 10631U;
	const unsigned int kr = (3000U * z1 - tsh[t]) / 1063100U - !z1;
	return z1 - (((int)kr * 1063100 + tsh[t]) / 3000) + !z1 != 1;
}

static __attribute__((const, pure)) inline unsigned int
__ndim_hij(hij_typ_t t, hij_epo_t e, unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y. */
	return 29 + (m % 2U) + (m == 12U && __hij_inty_p(t, e, y));
}

static __attribute__((const, pure)) echs_wday_t
__wday_greg(unsigned int y, unsigned int m, unsigned int d)
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
__wday_ht(
	const unsigned int *cal, size_t nm,
	unsigned int y, unsigned int m, unsigned int d)
{
	const mjd_t j = ht2mjd(cal, nm, (struct ymd_s){y, m, d});
	return (echs_wday_t)(((j + 1U) % 7U) + 1U);
}

static __attribute__((const, pure)) echs_wday_t
__wday_hij(
	hij_typ_t t, hij_epo_t e,
	unsigned int y, unsigned int m, unsigned int d)
{
	const mjd_t j = hij2mjd(t, e, (struct ymd_s){y, m, d});
	return (echs_wday_t)(((j + 1U) % 7U) + 1U);
}


__attribute__((pure, const)) unsigned int
echs_scale_ndim(echs_scale_t s, unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y using scale S. */
	switch (s) {
	case SCALE_GREGORIAN:
		return __ndim_greg(y, m);
	case SCALE_HIJRI_IA:
	case SCALE_HIJRI_IC:
	case SCALE_HIJRI_IIA:
	case SCALE_HIJRI_IIC:
	case SCALE_HIJRI_IIIA:
	case SCALE_HIJRI_IIIC:
	case SCALE_HIJRI_IVA:
	case SCALE_HIJRI_IVC:
		return __ndim_hij(SCAL2TYP(s), SCAL2EPO(s), y, m);
	case SCALE_HIJRI_UMMULQURA:
		return __ndim_ht(dat_ummulqura, NM(dat_ummulqura), y, m);
	case SCALE_HIJRI_DIYANET:
		return __ndim_ht(dat_diyanet, NM(dat_diyanet), y, m);
	default:
		break;
	}
	return 0U;
}

__attribute__((pure, const)) echs_wday_t
echs_scale_wday(echs_scale_t s, unsigned int y, unsigned int m, unsigned int d)
{
	switch (s) {
	case SCALE_GREGORIAN:
		return __wday_greg(y, m, d);
	case SCALE_HIJRI_IA:
	case SCALE_HIJRI_IC:
	case SCALE_HIJRI_IIA:
	case SCALE_HIJRI_IIC:
	case SCALE_HIJRI_IIIA:
	case SCALE_HIJRI_IIIC:
	case SCALE_HIJRI_IVA:
	case SCALE_HIJRI_IVC:
		return __wday_hij(SCAL2TYP(s), SCAL2EPO(s), y, m, d);
	case SCALE_HIJRI_UMMULQURA:
		return __wday_ht(dat_ummulqura, NM(dat_ummulqura), y, m, d);
	case SCALE_HIJRI_DIYANET:
		return __wday_ht(dat_diyanet, NM(dat_diyanet), y, m, d);
	}
	return MIR;
}

echs_instant_t
echs_instant_rescale(echs_instant_t i, echs_scale_t tgt)
{
	const echs_scale_t src = echs_instant_scale(i);
	const echs_tzob_t z = echs_instant_tzob(i);
	echs_instant_t tmp =
		echs_instant_detach_scale(echs_instant_detach_tzob(i));
	const struct ymd_s ymp = {tmp.y, tmp.m, tmp.d};

	if (src != tgt) {
		mjd_t d;
		struct ymd_s tgg;

		switch (src) {
		case SCALE_GREGORIAN:
			d = g2mjd(ymp);
			break;
		case SCALE_HIJRI_IA:
		case SCALE_HIJRI_IC:
		case SCALE_HIJRI_IIA:
		case SCALE_HIJRI_IIC:
		case SCALE_HIJRI_IIIA:
		case SCALE_HIJRI_IIIC:
		case SCALE_HIJRI_IVA:
		case SCALE_HIJRI_IVC:
			d = hij2mjd(SCAL2TYP(src), SCAL2EPO(src), ymp);
			break;
		case SCALE_HIJRI_UMMULQURA:
			d = ht2mjd(dat_ummulqura, NM(dat_ummulqura), ymp);
			break;
		case SCALE_HIJRI_DIYANET:
			d = ht2mjd(dat_diyanet, NM(dat_diyanet), ymp);
			break;
		default:
			goto nul;
		}
		/* now convert to TGT scale */
		switch (tgt) {
		case SCALE_GREGORIAN:
			tgg = mjd2g(d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		case SCALE_HIJRI_IA:
		case SCALE_HIJRI_IC:
		case SCALE_HIJRI_IIA:
		case SCALE_HIJRI_IIC:
		case SCALE_HIJRI_IIIA:
		case SCALE_HIJRI_IIIC:
		case SCALE_HIJRI_IVA:
		case SCALE_HIJRI_IVC:
			tgg = mjd2hij(SCAL2TYP(tgt), SCAL2EPO(tgt), d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		case SCALE_HIJRI_UMMULQURA:
			tgg = mjd2ht(dat_ummulqura, NM(dat_ummulqura), d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		case SCALE_HIJRI_DIYANET:
			tgg = mjd2ht(dat_diyanet, NM(dat_diyanet), d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		default:
			goto nul;
		}
		/* assign to tgt instant */
		tmp.y = tgg.y;
		tmp.m = tgg.m;
		tmp.d = tgg.d;
	}
	return echs_instant_attach_tzob(echs_instant_attach_scale(tmp, tgt), z);
nul:
	return echs_nul_instant();
}

/* scale.c ends here */
