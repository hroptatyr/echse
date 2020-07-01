/*** zoneinfo.c -- dst switches as event stream
 *
 * Copyright (C) 2020 Sebastian Freundt
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
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <limits.h>
#include "echse.h"
#include "instant.h"
#include "yd.h"
#include "boobs.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

#if !defined MAP_ANONYMOUS && defined MAP_ANON
# define MAP_ANONYMOUS	(MAP_ANON)
#endif	/* MAP_ANON->MAP_ANONYMOUS */
#define PROT_MEMMAP	PROT_READ | PROT_WRITE
#define MAP_MEMMAP	MAP_PRIVATE | MAP_ANONYMOUS

#if defined TZDIR
static const char tzdir[] = TZDIR;
#else  /* !TZDIR */
static const char tzdir[] = "/usr/share/zoneinfo";
#endif	/* TZDIR */

typedef struct zif_s *zif_t;
typedef char *znam_t;
typedef int32_t *ztr_t;
typedef uint8_t *zty_t;
typedef struct ztrdtl_s *ztrdtl_t;

/* convenience struct where we copy all the good things into one */
struct zspec_s {
	int32_t since;
	unsigned int offs:31;
	unsigned int dstp:1;
	znam_t name;
} __attribute__((packed, aligned(16)));

/* this is tzhead but better */
struct zih_s {
	/* magic */
	char tzh_magic[4];
	/* must be '2' now, as of 2005 */
	char tzh_version[1];
	/* reserved--must be zero */
	char tzh_reserved[15];
	/* number of transition time flags in gmt */
	uint32_t tzh_ttisgmtcnt;
	/* number of transition time flags in local time */
	uint32_t tzh_ttisstdcnt;
	/* number of recorded leap seconds */
	uint32_t tzh_leapcnt;
	/* number of recorded transition times */
	uint32_t tzh_timecnt;
	/* number of local time type */
	uint32_t tzh_typecnt;
	/* number of abbreviation chars */
	uint32_t tzh_charcnt;
};

/* this one must be packed to account for the packed file layout */
struct ztrdtl_s {
	int32_t offs;
	uint8_t dstp;
	uint8_t abbr;
} __attribute__((packed));

/* leap second support missing */
struct zif_s {
	size_t mpsz;
	struct zih_s *hdr;

	/* transitions */
	ztr_t trs;
	/* types */
	zty_t tys;
	/* type array, deser'd, transition details array */
	ztrdtl_t tda;
	/* zonename array */
	znam_t zn;

	/* file descriptor, if >0 this also means all data is in BE */
	int fd;
};

typedef enum {
	USE_DEFLT,
	USE_ZNAME,
	USE_UOFFS,
} flavour_t;

struct clo_s {
	/* the zoneinfo file handle */
	zif_t zi;
	/* now in unix speak */
	int32_t now;
	/* transition index of/before now */
	int tri;

	/* what WHAT to use */
	flavour_t flav:8;

	/* for our own state keeping */
	unsigned int anncdp:1;

	/* buffer for the last WHAT including the ~ upfront */
	char last[64];
};


/* instant <-> timestamp helpers */
static int32_t
__inst_to_unix(echs_instant_t i)
{
	struct yd_s yd = __md_to_yd(i.y, (struct md_s){.m = i.m, .d = i.d});
	int y = i.y - 1970;
	int j00 = y * 365 + (y - 2) / 4;

	return ((((j00 + yd.d) * 24 + i.H) * 60 + i.M) * 60 + i.S);
}

static echs_instant_t
__unix_to_inst(int32_t ts)
{
/* stolen from dateutils' daisy.c */
	echs_instant_t i;
	int y;
	int j00;
	unsigned int doy;
	int d;
	int h;

	/* decompose */
	d = ts / 86400;
	h = ts % 86400;
	if (UNLIKELY(ts < 0)) {
		d--;
		h += 86400;
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
	i.y = y + 1970U;
	/* this one must be positive now */
	doy = d - j00;

	/* get month and day from doy */
	{
		struct md_s md = __yd_to_md((struct yd_s){i.y, doy});
		i.m = md.m;
		i.d = md.d;
	}

	/* account for H M S now */
	i.S = h % 60;
	h /= 60;
	i.M = h % 60;
	h /= 60;
	i.H = h;
	return i;
}


/**
 * Return the total number of transitions in zoneinfo file Z. */
static inline size_t
zif_ntrans(zif_t z)
{
	return z->hdr->tzh_timecnt;
}

/**
 * Return the transition time stamp of the N-th transition in Z. */
static inline int32_t
zif_trans(zif_t z, int n)
{
/* no bound check! */
	return zif_ntrans(z) > 0UL ? z->trs[n] : INT_MIN;
}

/**
 * Return the total number of transition types in zoneinfo file Z. */
static inline size_t
zif_ntypes(zif_t z)
{
	return z->hdr->tzh_typecnt;
}

/**
 * Return the transition type index of the N-th transition in Z. */
static inline uint8_t
zif_type(zif_t z, int n)
{
/* no bound check! */
	return (uint8_t)(zif_ntrans(z) > 0UL ? z->tys[n] : 0U);
}

/**
 * Return the total number of transitions in zoneinfo file Z. */
static inline size_t
zif_nchars(zif_t z)
{
	return z->hdr->tzh_charcnt;
}

/**
 * Return the transition details after the N-th transition in Z. */
static inline __attribute__((unused)) struct ztrdtl_s
zif_trdtl(zif_t z, int n)
{
/* no bound check! */
	struct ztrdtl_s res;
	uint8_t idx = zif_type(z, n);
	res = z->tda[idx];
	res.offs = z->tda[idx].offs;
	return res;
}

/**
 * Return the gmt offset the N-th transition in Z. */
static inline __attribute__((unused)) int32_t
zif_troffs(zif_t z, int n)
{
/* no bound check! */
	uint8_t idx = zif_type(z, n);
	return z->tda[idx].offs;
}

/**
 * Return the total number of leap second transitions. */
static inline __attribute__((unused)) size_t
zif_nleaps(zif_t z)
{
	return z->hdr->tzh_leapcnt;
}

/**
 * Return the zonename after the N-th transition in Z. */
static inline __attribute__((unused)) znam_t
zif_trname(zif_t z, int n)
{
/* no bound check! */
	uint8_t idx = zif_type(z, n);
	uint8_t jdx = z->tda[idx].abbr;
	return z->zn + jdx;
}

/**
 * Return a succinct summary of the situation after transition N in Z. */
static inline struct zspec_s
zif_spec(zif_t z, int n)
{
	struct zspec_s res;
	uint8_t idx = zif_type(z, n);
	uint8_t jdx = z->tda[idx].abbr;

	res.since = zif_trans(z, n);
	res.offs = z->tda[idx].offs;
	res.dstp = z->tda[idx].dstp;
	res.name = z->zn + jdx;
	return res;
}


static int
__open_zif(const char *file)
{
	if (file == NULL || file[0] == '\0') {
		return -1;
	}

	if (file[0] != '/') {
		/* not an absolute file name */
		size_t len = strlen(file) + 1;
		size_t tzd_len = sizeof(tzdir) - 1;
		char *new, *tmp;

		new = alloca(tzd_len + 1 + len);
		memcpy(new, tzdir, tzd_len);
		tmp = new + tzd_len;
		*tmp++ = '/';
		memcpy(tmp, file, len);
		file = new;
	}
	return open(file, O_RDONLY, 0644);
}

static void
__init_zif(zif_t z)
{
	size_t ntr;
	size_t nty;

	if (z->fd > STDIN_FILENO) {
		/* probably in BE then, eh? */
		ntr = be32toh(zif_ntrans(z));
		nty = be32toh(zif_ntypes(z));
	} else {
		ntr = zif_ntrans(z);
		nty = zif_ntypes(z);
	}

	z->trs = (ztr_t)(z->hdr + 1);
	z->tys = (zty_t)(z->trs + ntr);
	z->tda = (ztrdtl_t)(z->tys + ntr);
	z->zn = (char*)(z->tda + nty);
	return;
}

static int
__read_zif(struct zif_s *tgt, int fd)
{
	struct stat st;

	if (fstat(fd, &st) < 0) {
		return -1;
	} else if (st.st_size <= 4) {
		return -1;
	}
	tgt->mpsz = st.st_size;
	tgt->fd = fd;
	tgt->hdr = mmap(NULL, tgt->mpsz, PROT_READ, MAP_SHARED, fd, 0);
	if (tgt->hdr == MAP_FAILED) {
		return -1;
	}
	/* all clear so far, populate */
	__init_zif(tgt);
	return 0;
}

static void
__conv_hdr(struct zih_s *restrict tgt, const struct zih_s *src)
{
/* copy SRC to TGT doing byte-order conversions on the way */
	memcpy(tgt, src, offsetof(struct zih_s, tzh_ttisgmtcnt));
	tgt->tzh_ttisgmtcnt = be32toh(src->tzh_ttisgmtcnt);
	tgt->tzh_ttisstdcnt = be32toh(src->tzh_ttisstdcnt);
	tgt->tzh_leapcnt = be32toh(src->tzh_leapcnt);
	tgt->tzh_timecnt = be32toh(src->tzh_timecnt);
	tgt->tzh_typecnt = be32toh(src->tzh_typecnt);
	tgt->tzh_charcnt = be32toh(src->tzh_charcnt);
	return;
}

static void
__conv_zif(zif_t tgt, zif_t src)
{
	size_t ntr;
	size_t nty;
	size_t nch;

	/* convert header to hbo */
	__conv_hdr(tgt->hdr, src->hdr);

	/* everything in host byte-order already */
	ntr = zif_ntrans(tgt);
	nty = zif_ntypes(tgt);
	nch = zif_nchars(tgt);
	__init_zif(tgt);

	/* transition vector */
	for (size_t i = 0; i < ntr; i++) {
		tgt->trs[i] = be32toh(src->trs[i]);
	}

	/* type vector, nothing to byte-swap here */
	memcpy(tgt->tys, src->tys, ntr * sizeof(*tgt->tys));

	/* transition details vector */
	for (size_t i = 0; i < nty; i++) {
		tgt->tda[i].offs = be32toh(src->tda[i].offs);
		tgt->tda[i].dstp = src->tda[i].dstp;
		tgt->tda[i].abbr = src->tda[i].abbr;
	}

	/* zone name array */
	memcpy(tgt->zn, src->zn, nch * sizeof(*tgt->zn));
	return;
}

static zif_t
__copy_conv(zif_t z)
{
/* copy Z and do byte-order conversions */
	static size_t pgsz = 0;
	size_t prim;
	size_t tot_sz;
	zif_t res = NULL;

	/* singleton */
	if (!pgsz) {
		pgsz = sysconf(_SC_PAGESIZE);
	}
	/* compute a size */
	prim = z->mpsz + sizeof(*z);
	/* round up to page size and alignment */
	tot_sz = ((prim + 16) + (pgsz - 1)) & ~(pgsz - 1);

	/* we'll mmap ourselves a slightly larger struct so
	 * res + 1 points to the header, while res + 0 is the zif_t */
	res = mmap(NULL, tot_sz, PROT_MEMMAP, MAP_MEMMAP, -1, 0);
	if (UNLIKELY(res == MAP_FAILED)) {
		return NULL;
	}
	/* great, now to some initial assignments */
	res->mpsz = tot_sz;
	res->hdr = (void*)(res + 1);
	/* make sure we denote that this isnt connected to a file */
	res->fd = -1;

	/* convert the header and payload now */
	__conv_zif(res, z);

	/* that's all :) */
	return res;
}


static zif_t zif_open(const char *file);
static void zif_close(zif_t z);
static zif_t zif_copy(zif_t z);

static zif_t
zif_open(const char *file)
{
	int fd;
	struct zif_s tmp[1];
	zif_t res;

	if (UNLIKELY((fd = __open_zif(file)) <= STDIN_FILENO)) {
		return NULL;
	} else if (UNLIKELY(__read_zif(tmp, fd) < 0)) {
		return NULL;
	}
	/* otherwise all's fine, it's still BE so convert to host byte-order */
	res = zif_copy(tmp);
	zif_close(tmp);
	return res;
}

static void
zif_close(zif_t z)
{
	if (UNLIKELY(z == NULL)) {
		/* nothing to do */
		return;
	}
	if (z->fd > STDIN_FILENO) {
		close(z->fd);
	}
	/* check if z is in mmap()'d space */
	if (z->hdr == MAP_FAILED) {
		/* not sure what to do */
		;
	} else if ((z + 1) != (void*)z->hdr) {
		/* z->hdr is mmapped, z is not */
		munmap((void*)z->hdr, z->mpsz);
	} else {
		munmap(z, z->mpsz);
	}
	return;
}

static zif_t
zif_copy(zif_t z)
{
/* copy Z into a newly allocated zif_t object
 * if applicable also perform byte-order conversions */
	zif_t res;

	if (UNLIKELY(z == NULL)) {
		/* no need to bother */
		return NULL;
	} else if (z->fd > STDIN_FILENO) {
		return __copy_conv(z);
	}
	/* otherwise it's a plain copy */
	res = mmap(NULL, z->mpsz, PROT_MEMMAP, MAP_MEMMAP, -1, 0);
	if (UNLIKELY(res == MAP_FAILED)) {
		return NULL;
	}
	memcpy(res, z, z->mpsz);
	__init_zif(res);
	return res;
}


/* finders */
static inline int
__find_trno(zif_t z, int32_t t, int this, int min, int max)
{
	do {
		int32_t tl, tu;

		if (UNLIKELY(max == 0)) {
			/* special case */
			return 0;
		}

		tl = zif_trans(z, this);
		tu = zif_trans(z, this + 1);

		if (t >= tl && t < tu) {
			/* found him */
			return this;
		} else if (max - 1 <= min) {
			/* nearly found him */
			return this + 1;
		} else if (t >= tu) {
			min = this + 1;
			this = (this + max) / 2;
		} else if (t < tl) {
			max = this - 1;
			this = (this + min) / 2;
		}
	} while (true);
	/* not reached */
}

static int
zif_find_trans(zif_t z, int32_t t)
{
/* find the last transition before time, time is expected to be UTC */
	int max = zif_ntrans(z);
	int min = 0;
	int this = max / 2;

	return __find_trno(z, t, this, min, max);
}


/* properties */
typedef enum {
	PROP_UNK,
	PROP_ZIFN,
	PROP_OUTPUT,
} prop_t;

static prop_t
__prop(const char *key, struct echs_pset_s pset)
{
	switch (pset.typ) {
	case ECHS_PSET_STR:
		if (!strcmp(key, ":file") || !strcmp(key, ":zone")) {
			return PROP_ZIFN;
		} else if (!strcmp(key, ":output")) {
			return PROP_OUTPUT;
		}
		break;
	default:
		break;
	}
	return PROP_UNK;
}

void
echs_stream_pset(echs_stream_t s, const char *key, struct echs_pset_s v)
{
	struct clo_s *clo = s.clo;
	zif_t zi;

	switch (__prop(key, v)) {
	case PROP_ZIFN:
		if (UNLIKELY(clo->zi != NULL)) {
			/* there's a zone file there already, fuck right off */
			;
		} else if (LIKELY((zi = zif_open(v.str)) != NULL)) {
			/* yay */
			clo->zi = zi;
			clo->tri = zif_find_trans(zi, clo->now);
			if ((clo->tri++, (size_t)clo->tri >= zif_ntrans(zi))) {
				/* no states at all then? */
				;
			}
		}
		break;
	case PROP_OUTPUT:
		if (!strcmp(v.str, "zone")) {
			clo->flav = USE_ZNAME;
		} else if (!strcmp(v.str, "offset")) {
			clo->flav = USE_UOFFS;
		}
		break;
	default:
		break;
	}
	return;
}


static echs_event_t
__zi(void *vclo)
{
	DEFSTATE(DST);
	struct clo_s *clo = vclo;
	struct zspec_s dtl;
	echs_event_t e;

	/* since zoneinfo is sort of like an echse stream
	 * all that needs doing is to advance the transition index */
	if (UNLIKELY(clo->zi == NULL)) {
		/* bugger off */
		return (echs_event_t){0};
	} else if ((!clo->anncdp || clo->tri++,
		    (size_t)clo->tri >= zif_ntrans(clo->zi))) {
		return (echs_event_t){0};
	}
	/* everything in order, proceed normally */
	dtl = zif_spec(clo->zi, clo->tri);
	e.when = __unix_to_inst(dtl.since);
	switch (clo->flav) {
	default:
	case USE_DEFLT:
		if (dtl.dstp) {
			e.what = ON(DST);
		} else {
			e.what = OFF(DST);
		}
		clo->anncdp = 1;
		break;

	case USE_ZNAME:
		e.what = clo->last;
		if (!clo->anncdp++) {
			const char *what = dtl.name;
			size_t whaz = strlen(what);

			memcpy(clo->last + 1, what, whaz);
			clo->last[1 + whaz] = '\0';
			e.what++;
		}
		break;

	case USE_UOFFS:
		e.what = clo->last;
		if (!clo->anncdp++) {
			char *p = clo->last + 1;
			size_t z = sizeof(clo->last) - 1;

			snprintf(p, z, "OFFSET=%d", dtl.offs);
			e.what++;
		}
		break;
	}
	return e;
}

echs_stream_t
make_echs_stream(echs_instant_t inst, ...)
{
	struct clo_s *clo;
	int32_t its;

	/* roll forward to the transition in question */
	its = __inst_to_unix(inst);

	/* everything seems in order, prep the closure */
	clo = calloc(1, sizeof(*clo));
	clo->now = its;
	clo->last[0] = '~';
	return (echs_stream_t){__zi, clo};
}

void
free_echs_stream(echs_stream_t s)
{
	struct clo_s *clo = s.clo;

	if (UNLIKELY(clo == NULL)) {
		return;
	} else if (clo->zi != NULL) {
		zif_close(clo->zi);
		clo->zi = NULL;
	}
	free(clo);
	return;
}

/* zoneinfo.c ends here */
