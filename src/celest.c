/*** celest.c -- goodies for rise, transit, set computations
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
/* These computations follow: http://www.stjarnhimlen.se/comp/riset.html */
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include "celest.h"
#include "float.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

#if defined __INTEL_COMPILER
# pragma warning (disable:981)
#endif	/* __INTEL_COMPILER */

#define DEFCEL_OBJ(x)		cel_obj_t x = &(struct cel_obj_s)

typedef struct rasc_decl_s rasc_decl_t;

struct orb_s {
	double N;
	double i;
	double w;
	double a;
	double e;
	double M;
};

struct cel_obj_s {
	double N[2];
	double i[2];
	double w[2];
	double a[2];
	double e[2];
	double M[2];
};

struct rasc_decl_s {
	double rasc;
	double decl;

	double gmst0;
};


struct decomp_s {
	int ip;
	double r;
};

static inline __attribute__((pure, const)) double
fmod_2pi(double x)
{
	if (x > 0.0) {
		return fmod(x, 2 * pi);
	} else {
		return 2 * pi + fmod(x, 2 * pi);
	}
}

static inline __attribute__((pure)) struct decomp_s
decomp(double x)
{
	double ip;
	double r = modf(x, &ip);
	return (struct decomp_s){.ip = (int)ip, .r = r};
}


static struct orb_s
orb_scalprod(cel_obj_t o, double d)
{
/* take the scalar product of orbit vector o and [1, d] */
	struct orb_s res = {
		.N = o->N[0] + d * o->N[1],
		.i = o->i[0] + d * o->i[1],
		.w = o->w[0] + d * o->w[1],
		.a = o->a[0] + d * o->a[1],
		.e = o->e[0] + d * o->e[1],
		.M = o->M[0] + d * o->M[1],
	};
	return res;
}

static double
ecl_earth(double d)
{
	return RAD(23.4393) - RAD(3.563E-7) * d;
}

static double
transit(rasc_decl_t rd, cel_pos_t p)
{
	return fmod_2pi(rd.rasc - rd.gmst0 - p.lng);
}

static double
cos_lha(rasc_decl_t rd, cel_pos_t p)
{
/* cosine of sun's local hour angle */
	const double h = RAD(-0.833);

	return (sin(h) - sin(p.lat) * sin(rd.decl)) /
		(cos(p.lat) * cos(rd.decl));
}

static struct rasc_decl_s
rasc_decl(cel_obj_t obj, double d)
{
	struct orb_s o = orb_scalprod(obj, d);
	double ecl = ecl_earth(d);

	double E = o.M + o.e * sin(o.M) * (1.0 + o.e * cos(o.M));
	double xv = cos(E) - o.e;
	double yv = sin(E) * sqrt(1.0 - o.e * o.e);

	double v = atan2(yv, xv);
	double r = sqrt(xv * xv + yv * yv);
	double lng_sun = v + o.w;

	double xs = r * cos(lng_sun);
	double ys = r * sin(lng_sun);
	double UNUSED(zs) = 0.0;

	double xe = xs;
	double ye = ys * cos(ecl);
	double ze = ys * sin(ecl);

	/* final result */
	struct rasc_decl_s res = {
		.rasc = atan2(ye, xe),
		.decl = atan2(ze, sqrt(xe * xe + ye * ye)),
		.gmst0 = o.M + o.w + pi,
	};

	return res;
}


/* little data base of objects */
DEFCEL_OBJ(sun)
{
	.N[0] = 0.0,
	.i[0] = 0.0,
	.w[0] = RAD(282.9404), .w[1] = RAD(4.70935e-5),
	.a[0] = 1.0,
	.e[0] = 0.016709, .e[1] = - 1.151e-9,
	.M[0] = RAD(356.0470), .M[1] = RAD(0.9856002585),
};

DEFCEL_OBJ(moon)
{
	.N[0] = 0.0,
	.N[0] = RAD(125.1228), .N[1] = RAD(-0.0529538083),
	.i[0] = 5.1454,
	.w[0] = RAD(318.0634), .w[1] = RAD(0.1643573223),
	.a[0] = 60.2666,
	.e[0] = 0.054900,
	.M[0] = RAD(115.3654), .M[1] = RAD(13.0649929509),
};


cel_d_t
instant_to_d(echs_instant_t i)
{
	unsigned int d = 367U * i.y -
		7U * (i.y + (i.m + 9U) / 12U) / 4U + 275U * i.m / 9U + i.d;
	return d - 730530;
}

static __attribute__((unused)) cel_h_t
instant_to_h(echs_instant_t i)
{
	unsigned int s = ((i.H * 60U + i.M) * 60U + i.S) * 1000U + i.ms;
	return (double)s / 86400000.0;
}

echs_instant_t
dh_to_instant(cel_d_t d, cel_h_t h)
{
/* stolen from dateutils' daisy.c */
	echs_instant_t i;
	int y;
	int j00;
	unsigned int doy;

	/* bring h into shape */
	{
		struct decomp_s dh = decomp((double)d + h);
		d = dh.ip;
		h = dh.r;
	}

	/* get year first (estimate) */
	y = --d / 365U;
	/* get jan-00 of (est.) Y */
	j00 = y * 365U + y / 4U;
	/* y correct? */
	if (UNLIKELY(j00 > d)) {
		/* correct y */
		y--;
		/* and also recompute the j00 of y */
		j00 = y * 365U + y / 4U;
	}
	/* ass */
	i.y = y + 2000U;
	/* this one must be positive now */
	doy = d - j00;

	/* get month and day from doy */
	{
#define GET_REM(x)	(rem[x])
		static const uint8_t rem[] = {
			19, 19, 18, 14, 13, 11, 10, 8, 7, 6, 4, 3, 1, 0

		};
		unsigned int mdm;
		unsigned int mdd;
		unsigned int beef;
		unsigned int cake;

		/* get 32-adic doys */
		mdm = (doy + 19) / 32U;
		mdd = (doy + 19) % 32U;
		beef = GET_REM(mdm);
		cake = GET_REM(mdm + 1);

		/* put leap years into cake */
		if (UNLIKELY((y % 4) == 0U && cake < 16U)) {
			/* note how all leap-affected cakes are < 16 */
			beef += beef < 16U;
			cake++;
		}

		if (mdd <= cake) {
			mdd = doy - ((mdm - 1) * 32 - 19 + beef);
		} else {
			mdd = doy - (mdm++ * 32 - 19 + cake);
		}
		i.m = mdm;
		i.d = mdd;
#undef GET_REM
	}

	/* account for H M S now */
	{
		struct decomp_s ipr;

		i.H = (ipr = decomp(h * 24.0)).ip;
		i.M = (ipr = decomp(ipr.r * 60.0)).ip;
		i.S = (ipr = decomp(ipr.r * 60.0)).ip;
		i.ms = (unsigned int)(ipr.r * 1000.0);
	}
	return i;
}

cel_rts_t
cel_rts(cel_obj_t obj, cel_d_t d, cel_pos_t p)
{
#define MAX_ITER	(4U)
	struct cel_rts_s res = {
		.transit = 0.0,
	};
	double t;
	double ot;

	t = ot = NAN;
	for (size_t i = 0; !(fabs(t - ot) < DBL_EPSILON) && i < MAX_ITER; i++) {
		rasc_decl_t rd;

		ot = t;
		rd = rasc_decl(obj, (double)d + res.transit);
		t = transit(rd, p);
		res.transit = t / (2 * pi);
	}

	t = ot = NAN;
	res.rise = res.transit;
	for (size_t i = 0; !(fabs(t - ot) < DBL_EPSILON) && i < MAX_ITER; i++) {
		rasc_decl_t rd;
		double tmp;

		ot = t;
		rd = rasc_decl(obj, (double)d + res.rise);
		if (isnan(tmp = acos(cos_lha(rd, p)))) {
			res.rise = NAN;
			break;
		}
		res.rise = t = res.transit - tmp / (2 * pi);
	}

	t = ot = NAN;
	res.set = res.transit;
	for (size_t i = 0; !(fabs(t - ot) < DBL_EPSILON) && i < MAX_ITER; i++) {
		rasc_decl_t rd;
		double tmp;

		ot = t;
		rd = rasc_decl(obj, (double)d + res.set);
		if (isnan(tmp = acos(cos_lha(rd, p)))) {
			res.set = NAN;
			break;
		}
		res.set = t = res.transit + tmp / (2 * pi);
	}

	return res;
}

/* celest.c ends here */
