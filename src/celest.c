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
#include <math.h>
#include <stdbool.h>
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

typedef double cel_jdd_t;
typedef double cel_lst_t;
typedef struct cyl_pos_s cyl_pos_t;
typedef struct rasc_decl_s rasc_decl_t;

struct orb_s {
	/* longitude of the ascending node */
	double N;
	/* inclination */
	double i;

	/* semi-major axis */
	double a;
	/* eccentricity */
	double e;

	/* argument of periapsis */
	double w;
	/* mean anomaly at epoch */
	double M;
};

struct cel_obj_s {
	double N[2];
	double i[2];

	double a[2];
	double e[2];

	double w[2];
	double M[2];

	double app_d;

	cyl_pos_t(*fixup)(cyl_pos_t, struct orb_s oo, struct orb_s os);
};

struct cyl_pos_s {
	double r;
	double lng;
	double lat;
};


struct decomp_s {
	int ip;
	double r;
};

static inline __attribute__((pure, const)) double
fmod_360(double x)
{
	if (x > 0.0) {
		return fmod(x, 360);
	} else {
		return 360 + fmod(x, 360);
	}
}

static inline __attribute__((pure)) struct decomp_s
decomp(double x)
{
	double ip;
	double r = modf(x, &ip);
	struct decomp_s res = {
		.ip = (int)ip,
		.r = r
	};
	if (UNLIKELY(res.r < 0.0)) {
		res.ip--;
		res.r += 1.0;
	}
	return res;
}

static inline __attribute__((pure)) bool
prec_eq_p(double x1, double x2, double prec)
{
	return fabs(x1 - x2) < prec;
}

/* converter earth radii <-> astronomical units */
#define ER_TO_AU(x)	((x) / 23481.065876628472)
#define AU_TO_ER(x)	((x) * 23481.065876628472)


static struct orb_s
orb_scalprod(cel_obj_t o, cel_jdd_t d)
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
ecl_earth(cel_jdd_t d)
{
	return RAD(23.4406) - RAD(3.563E-7) * d;
}

static double
parallax(double r)
{
	return asin(1 / AU_TO_ER(r));
}

static cel_lst_t
get_lst(cel_lst_t gmst0, cel_h_t ut, cel_pos_t p)
{
	return gmst0 + ut * 15.0 * 24.0 + DEG(p.lng);
}

static cel_h_t
get_lth(cel_lst_t gmst0, cel_lst_t gmst, cel_pos_t p)
{
	return fmod_360(gmst - gmst0 - DEG(p.lng)) / 15.04107 / 24.0;
}

static cyl_pos_t
cyl_pos_sun(cel_jdd_t d)
{
	struct orb_s o = orb_scalprod(sun, d);

	double E = o.M + o.e * sin(o.M) * (1.0 + o.e * cos(o.M));
	double xv = o.a * (cos(E) - o.e);
	double yv = o.a * (sin(E) * sqrt(1.0 - o.e * o.e));

	double v = atan2(yv, xv);
	double r = sqrt(xv * xv + yv * yv);
	double lng_sun = v + o.w;

	double xs = r * cos(lng_sun);
	double ys = r * sin(lng_sun);
	double UNUSED(zs) = 0.0;

	struct cyl_pos_s res = {
		.r = r,
		.lng = atan2(ys, xs),
		.lat = 0.0,
	};
	return res;
}

static double
cyl_pos_E(double e, double M)
{
	double E;
	double ol_E = NAN;

	E = M + e * sin(M) * (1.0 + e * cos(M));
	for (size_t i = 0; i < 20 && !prec_eq_p(E, ol_E, FLT_EPSILON); i++) {
		ol_E = E;
		E = E - (E - e * sin(E) - M) / (1 - e * cos(E));
	}
	return E;
}

static cyl_pos_t
cyl_pos_obj(cel_obj_t obj, cel_jdd_t d)
{
	struct orb_s o = orb_scalprod(obj, d);

	double E = cyl_pos_E(o.e, o.M);
	double xv = o.a * (cos(E) - o.e);
	double yv = o.a * (sin(E) * sqrt(1.0 - o.e * o.e));

	double v = atan2(yv, xv);
	double r = sqrt(xv * xv + yv * yv);
	double lng = v + o.w;

	double xh = r * (cos(o.N) * cos(lng) - sin(o.N) * sin(lng) * cos(o.i));
	double yh = r * (sin(o.N) * cos(lng) + cos(o.N) * sin(lng) * cos(o.i));
	double zh = r * sin(lng) * sin(o.i);

	/* for corrections */
	double lngecl = atan2(yh, xh);
	double latecl = atan2(zh, sqrt(xh * xh + yh * yh));

	struct cyl_pos_s res = {
		.lng = lngecl,
		.lat = latecl,
		.r = r,
	};
	return res;
}

static cyl_pos_t
geo_cnt_pos(struct cyl_pos_s pos, struct cyl_pos_s sun)
{
	double xh = pos.r * cos(pos.lng) * cos(pos.lat);
	double yh = pos.r * sin(pos.lng) * cos(pos.lat);
	double zh = pos.r * sin(pos.lat);

	double xs = sun.r * cos(sun.lng);
	double ys = sun.r * sin(sun.lng);

	double xg = xh + xs;
	double yg = yh + ys;
	double zg = zh;

	struct cyl_pos_s res = {
		.lng = atan2(yg, xg),
		.lat = atan2(zg, sqrt(xg * xg + yg * yg)),
		.r = sqrt(xg * xg + yg * yg + zg * zg),
	};
	return res;
}

static cyl_pos_t
geo_equ_pos(struct cyl_pos_s geo_cnt, cel_jdd_t d)
{
	double ecl = ecl_earth(d);

	double xh = geo_cnt.r * cos(geo_cnt.lng) * cos(geo_cnt.lat);
	double yh = geo_cnt.r * sin(geo_cnt.lng) * cos(geo_cnt.lat);
	double zh = geo_cnt.r * sin(geo_cnt.lat);

	double xe = xh;
	double ye = yh * cos(ecl) - zh * sin(ecl);
	double ze = yh * sin(ecl) + zh * cos(ecl);

	/* geocentric distance */
	double r = sqrt(xe * xe + ye * ye + ze * ze);
	/* longitude, aka right ascension */
	double lng = atan2(ye, xe);
	/* latitude, aka declination */
	double lat = atan2(ze, sqrt(xe * xe + ye * ye));

	struct cyl_pos_s res = {
		.lng = lng,
		.lat = lat,
		.r = r,
	};
	return res;
}

static cyl_pos_t
geo_top_pos(struct cyl_pos_s geo_cnt, cel_lst_t gmst0, cel_h_t ut, cel_pos_t p)
{
	cel_lst_t lst = get_lst(gmst0, ut, p);
	double par = parallax(geo_cnt.r);
	double gclat = p.lat;
	double rho = 1.0;

	double ha = RAD(lst) - geo_cnt.lng;
	double g = atan(tan(gclat) / cos(ha));

	double top_ra = geo_cnt.lng -
		par * rho * cos(gclat) * sin(ha) / cos(geo_cnt.lat);
	double top_decl = geo_cnt.lat -
		par * rho * sin(gclat) * sin(g - geo_cnt.lat) / sin(g);

	struct cyl_pos_s res = {
		.lng = top_ra,
		.lat = top_decl,
		.r = geo_cnt.r,
	};
	return res;
}

static cyl_pos_t
fixup_moon(cyl_pos_t p, struct orb_s moon, struct orb_s sun)
{
	double Ms = sun.M;
	double Mm = moon.M;
	double Ls = Ms + sun.w;
	double F = Mm + moon.w;
	double Lm = F + moon.N;
	double D = Lm - Ls;

	/* longitudal perbutations */
	/* (Evection) */
	p.lng += RAD(-1.274) * sin(Mm - 2 * D);
	/* (Variation) */
	p.lng += RAD(0.658) * sin(2 * D);
	/* (Yearly equation) */
	p.lng += RAD(-0.186) * sin(Ms);
	p.lng += RAD(-0.059) * sin(2 * Mm - 2 * D);
	p.lng += RAD(-0.057) * sin(Mm - 2 * D + Ms);
	p.lng += RAD(0.053) * sin(Mm + 2 * D);
	p.lng += RAD(0.046) * sin(2 * D - Ms);
	p.lng += RAD(0.041) * sin(Mm - Ms);
	/* (Parallactic equation) */
	p.lng += RAD(-0.035) * sin(D);
	p.lng += RAD(-0.031) * sin(Mm + Ms);
	p.lng += RAD(-0.015) * sin(2 * F - 2 * D);
	p.lng += RAD(0.011) * sin(Mm - 4 * D);

	/* latitudal perturbations */
	p.lat += RAD(-0.173) * sin(F - 2 * D);
	p.lat += RAD(-0.055) * sin(Mm - F - 2 * D);
	p.lat += RAD(-0.046) * sin(Mm + F - 2 * D);
	p.lat += RAD(0.033) * sin(F + 2 * D);
	p.lat += RAD(0.017) * sin(2 * Mm + F);

	/* distance perturbations */
	p.r += ER_TO_AU(-0.58) * cos(Mm - 2 * D);
	p.r += ER_TO_AU(-0.46) * cos(2 * D);
	return p;
}

static cel_lst_t
get_gmst0(cel_jdd_t d)
{
	struct orb_s o = orb_scalprod(sun, d);

	/* for gmst0 */
	double E = o.M + o.e * sin(o.M) * (1.0 + o.e * cos(o.M));
	double xv = o.a * (cos(E) - o.e);
	double yv = o.a * (sin(E) * sqrt(1.0 - o.e * o.e));
	double v = atan2(yv, xv);
	double Ls = v + o.w;
	double gmst0 = DEG(Ls) + 180;

	return fmod_360(gmst0);
}

static cyl_pos_t
obj_geo_cnt_pos(cel_obj_t obj, cel_jdd_t d)
{
	struct cyl_pos_s cp_obj;
	struct cyl_pos_s cp_sun;

	if (obj == sun) {
		cp_obj = (struct cyl_pos_s){0};
		cp_sun = cyl_pos_sun(d);
	} else if (obj == moon) {
		/* in earth-centric system already */
		return cyl_pos_obj(moon, d);
	} else {
		cp_obj = cyl_pos_obj(obj, d);
		cp_sun = cyl_pos_sun(d);
	}

	return geo_cnt_pos(cp_obj, cp_sun);
}

static cyl_pos_t
obj_geo_equ_pos(cel_obj_t obj, cel_jdd_t d)
{
	cyl_pos_t cnt = obj_geo_cnt_pos(obj, d);

	if (obj->fixup) {
		struct orb_s oo = orb_scalprod(obj, d);
		struct orb_s os = orb_scalprod(sun, d);

		cnt = obj->fixup(cnt, oo, os);
	}
	return geo_equ_pos(cnt, d);
}

static double
cos_lha(cyl_pos_t top, cel_pos_t p)
{
/* cosine of sun's local hour angle */
	double h = RAD(-0.833);

	return (sin(h) - sin(p.lat) * sin(top.lat)) /
		(cos(p.lat) * cos(top.lat));
}


/* little data base of objects */
DEFCEL_OBJ(sun)
{
	.N[0] = 0.0,
	.i[0] = 0.0,
	.a[0] = 1.0,
	.e[0] = 0.0167133, .e[1] = -1.151e-9,
	.w[0] = RAD(282.768500), .w[1] = RAD(4.70935e-5),
	.M[0] = RAD(356.237121), .M[1] = RAD(360.0 / 365.259641),

	.app_d = RAD(1919.26 / 60.0 / 60.0),
};

DEFCEL_OBJ(moon)
{
	.N[0] = RAD(125.1228), .N[1] = RAD(-0.0529538083),
	.i[0] = RAD(5.1454),
	.a[0] = ER_TO_AU(60.2666),
	.e[0] = 0.054900,
	.w[0] = RAD(318.0634), .w[1] = RAD(0.1643573223),
	.M[0] = RAD(115.3654), .M[1] = RAD(13.0649929509),

	.app_d = RAD(1873.70 / 60.0 / 60.0),

	.fixup = fixup_moon,
};


cel_d_t
instant_to_d(echs_instant_t i)
{
	static uint16_t __mon_yday[] = {
		/* this is \sum ml */
		0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};
	int y = i.y - 2000;
	int j00;
	int doy;

	j00 = y * 365 + y / 4;
	doy = __mon_yday[i.m - 1] + i.d + (UNLIKELY((y % 4) == 0) && i.m >= 3);

	return j00 + doy;
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
	y = d / 365;
	/* get jan-00 of (est.) Y */
	j00 = y * 365 + y / 4;
	/* y correct? */
	if (UNLIKELY(j00 >= d)) {
		/* correct y */
		y--;
		/* and also recompute the j00 of y */
		j00 = y * 365 + y / 4;
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
cel_rts(cel_obj_t obj, cel_d_t d, cel_pos_t p, struct cel_calcopt_s opt)
{
	struct cel_rts_s res = {
		.transit = 0.0,
	};
	cel_h_t t;
	cel_h_t ot;
	cel_lst_t gmst0;
	/* options */
	double prec;
	size_t max_iter;

	if ((max_iter = opt.max_iter) == 0UL) {
		max_iter = 4UL;
	}
	if ((prec = opt.prec) <= 0.0) {
		prec = DBL_EPSILON;
	}

	t = 0.0;
	ot = NAN;
	for (size_t i = 0; !prec_eq_p(t, ot, prec) && i < max_iter; i++) {
		cel_jdd_t dh;
		cyl_pos_t equ;

		ot = t;
		dh = (cel_jdd_t)d + t;
		gmst0 = get_gmst0(dh);
		equ = obj_geo_equ_pos(obj, dh);
		if (obj == moon) {
			/* moon needs topocentric coords */
			equ = geo_top_pos(equ, gmst0, t, p);
		}
		t = get_lth(gmst0, DEG(equ.lng), p);
		res.transit = t;
		break;
	}

	t = res.rise = res.transit;
	ot = NAN;
	for (size_t i = 0; !prec_eq_p(t, ot, prec) && i < max_iter; i++) {
		cel_jdd_t dh;
		cyl_pos_t equ;
		double tmp;
		double clha;

		ot = t;
		dh = (cel_jdd_t)d + t;
		gmst0 = get_gmst0(dh);
		equ = obj_geo_equ_pos(obj, dh);
		if (obj == moon) {
			/* moon needs topocentric coords */
			equ = geo_top_pos(equ, gmst0, t, p);
		}
		if (isnan(tmp = acos(clha = cos_lha(equ, p)))) {
			res.rise = NAN;
			break;
		}
		t = get_lth(gmst0, DEG(tmp), p);
		res.rise = res.transit - t;
	}

	t = res.set = res.transit;
	ot = NAN;
	for (size_t i = 0; !prec_eq_p(t, ot, prec) && i < max_iter; i++) {
		cel_jdd_t dh;
		cyl_pos_t equ;
		double tmp;
		double clha;

		ot = t;
		dh = (cel_jdd_t)d + t;
		gmst0 = get_gmst0(dh);
		equ = obj_geo_equ_pos(obj, dh);
		if (obj == moon) {
			/* moon needs topocentric coords */
			equ = geo_top_pos(equ, gmst0, t, p);
		}
		if (isnan(tmp = acos(clha = cos_lha(equ, p)))) {
			res.set = NAN;
			break;
		}
		t = get_lth(gmst0, DEG(tmp), p);
		res.set = res.transit + t;
	}
	return res;
}

/* celest.c ends here */
