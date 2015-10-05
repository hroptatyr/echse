/*** tzob.c -- timezone interning system
 *
 * Copyright (C) 2014-2015 Sebastian Freundt
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
#include "tzob.h"
#include "tzraw.h"
#include "hash.h"
#include "nifty.h"

/* the beef table */
static struct {
	uint_fast32_t of;
	uint_fast32_t ob;
	hash_t hx;
} *sstk;
/* alloc size, 2-power */
static size_t zstk;
/* number of elements */
static size_t nstk;

/* the big string obarray */
static char *restrict obs;
/* alloc size, 2-power */
static size_t obz;
/* next ob */
static size_t obn;

/* the hx obarray, specs and size just like the string obarray */
static hash_t hxa[256U];
static size_t hxn;

/* MFU cache of zifs */
static struct {
	echs_tzob_t z;
	size_t cnt;
} tmfu[16U];
static zif_t zmfu[16U];

static void*
recalloc(void *buf, size_t nmemb_ol, size_t nmemb_nu, size_t membz)
{
	nmemb_ol *= membz;
	nmemb_nu *= membz;
	buf = realloc(buf, nmemb_nu);
	memset((uint8_t*)buf + nmemb_ol, 0, nmemb_nu - nmemb_ol);
	return buf;
}

static uint_fast32_t
bang_zone(const char *str, size_t len)
{
/* put STR (of length LEN) into string obarray, don't check for dups */
#define OBAR_MINZ	(1024U)
	/* make sure we pad with \0 bytes to the next 4-byte multiple */
	size_t pad = ((len / 4U) + 1U) * 4U;
	uint_fast32_t res;

	if (UNLIKELY(obn + pad >= obz)) {
		size_t nuz = (obz * 2U) ?: OBAR_MINZ;

		obs = recalloc(obs, obz, nuz, sizeof(*obs));
		obz = nuz;
		if (UNLIKELY(obs == NULL)) {
			obz = obn = 0U;
			return 0U;
		}
	}
	/* paste the string in question */
	memcpy(obs + (res = obn), str, len);
	/* inc the obn pointer */
	obn += pad;
	return res;
}

static inline echs_tzob_t
make_tzob(size_t x)
{
	x &= 0xffU;
	return ((x << 6U) ^ (x << 10U) ^ (x << 24U)) & 0xffffffffU;
}

static inline size_t
make_size(echs_tzob_t z)
{
	return ((z >> 24U) ^ (z >> 10U) ^ (z >> 6U)) & 0xffU;
}

static echs_tzob_t
bang_tzob(hash_t hx)
{
/* put HX into global tzob obarray and return its index
 * readily shifted to the needs of ECHS_DMASK and ECHS_IMASK */
	if (UNLIKELY(hxn >= countof(hxa))) {
		/* nope, let's do fuckall instead
		 * more than 256 timezones?  we're not THAT international */
		return 0U;
	}
	/* just push the hx in question and advance the counter */
	hxa[hxn++] = hx;
	return make_tzob(hxn);
}

static hash_t
retr_tzob(echs_tzob_t z)
{
	size_t i;

	if (UNLIKELY((i = make_size(z)) >= countof(hxa))) {
		/* huh? */
		return 0U;
	} else if (UNLIKELY(!i)) {
		/* mega huh? */
		return 0U;
	}
	/* otherwise just return what's in the array */
	return hxa[i - 1U];
}

static zif_t
__tzob_zif(echs_tzob_t zob)
{
/* return a zif_t object from ZOB. */
#define SWP(x, y)					\
	do {						\
		__typeof(x) paste(__tmp, __LINE__) = x;	\
		x = y;					\
		y = paste(__tmp, __LINE__);		\
	} while (0)
	size_t i;

	for (i = 0U; i < countof(tmfu) - 1U; i++) {
		if (UNLIKELY(!tmfu[i].z)) {
			/* found an empty guy */
			break;
		} else if (LIKELY(tmfu[i].z == zob)) {
			/* found him */
			if (i && tmfu[i - 1U].cnt < ++tmfu[i].cnt) {
				/* swap with predecessor because
				 * obviously item I is used more often now
				 * this will slowly let the most used items
				 * bubble up */
				SWP(tmfu[i - 1U], tmfu[i]);
				SWP(zmfu[i - 1U], zmfu[i]);
				i--;
			}
			return zmfu[i];
		}
	}
	/* `i' should be <=countof(tmfu) - 1U, so do the usual checks */
	const char *fn;
	zif_t this;

	if (tmfu[i].z == zob) {
		/* how lucky can we get? */
		this = zmfu[i];
	} else if (UNLIKELY((fn = echs_zone(zob)) == NULL)) {
		/* yea, bollocks */
		this = NULL;
	} else if (UNLIKELY((this = zif_open(fn)) == NULL)) {
		/* just resort to doing fuckall */
		this = NULL;
	} else {
		if (zmfu[i] != NULL) {
			/* clear the zif out */
			zif_close(zmfu[i]);
		}
		zmfu[i] = this;
		tmfu[i].z = zob;
		tmfu[i].cnt = 1U;
	}
	return this;
}

static time_t
__inst_to_epoch(echs_instant_t i)
{
	static const uint16_t __mon_yday[] = {
		/* this is \sum ml,
		 * first element is a bit set of leap days to add */
		0U, 0U,
		31U, 59U, 90U, 120U, 151U, 181U,
		212U, 243U, 273U, 304U, 334U, 365U
	};
	unsigned int by = i.y - 1917U;
	/* no bullshit years in our lifetime */
	unsigned int j0 = by * 365U + by / 4U;
	/* yday by lookup */
	unsigned int yd = (LIKELY(i.m <= 12U))
		? __mon_yday[i.m] + i.d + UNLIKELY(!(i.y % 4U) && i.m >= 3U)
		: 0U;

	return ((((j0 + yd - 19359U/*our epoch v unix epoch*/) * 24U +
		  (LIKELY(i.H <= 24U) ? i.H : 24U)) * 60U + i.M) * 60U) + i.S;
}


echs_tzob_t
echs_tzob(const char *str, size_t len)
{
#define SSTK_NSLOT	(256U)
#define SSTK_STACK	(4U * SSTK_NSLOT)
#define OBINT_MAX_LEN	(256U)

	if (UNLIKELY(len == 0U || len >= OBINT_MAX_LEN)) {
		/* don't bother */
		return 0U;
	}

	/* we take 9 probes per 32bit value, hx.idx shifted by 3bits each
	 * then try the next stack
	 * the first stack is 256 entries wide, the next stack is 1024
	 * bytes wide, but only hosts 768 entries because the probe is
	 * constructed so that the lowest 8bits are always 0. */
	const hash_t hx = hash(str, len);
	hash_t k = hx;

	/* just try what we've got */
	if (UNLIKELY(!zstk)) {
		zstk = SSTK_STACK;
		sstk = calloc(zstk, sizeof(*sstk));
	}

	/* here's the initial probe then */
	for (size_t j = 0U; j < 9U; j++, k >>= 3U) {
		const size_t off = k & 0xffU;

		if (sstk[off].hx == hx) {
			/* found him (or super-collision) */
			return sstk[off].ob;
		} else if (!sstk[off].hx) {
			/* found empty slot */
			sstk[off].of = bang_zone(str, len);
			sstk[off].ob = bang_tzob(hx);
			sstk[off].hx = hx;
			nstk++;
			return sstk[off].ob;
		}
	}

	for (size_t i = SSTK_NSLOT, m = 0x3ffU;; i <<= 2U, m <<= 2U, m |= 3U) {
		/* reset k */
		k = hx;

		if (UNLIKELY(i >= zstk)) {
			sstk = recalloc(sstk, zstk, i << 2U, sizeof(*sstk));
			zstk = i << 2U;

			if (UNLIKELY(sstk == NULL)) {
				zstk = 0UL, nstk = 0UL;
				break;
			}
		}

		/* here we probe within the top entries of the stack */
		for (size_t j = 0U; j < 9U; j++, k >>= 3U) {
			const size_t off = (i | k) & m;

			if (sstk[off].hx == hx) {
				/* found him (or super-collision) */
				return sstk[off].ob;
			} else if (!sstk[off].hx) {
				/* found empty slot */
				sstk[off].of = bang_zone(str, len);
				sstk[off].ob = bang_tzob(hx);
				sstk[off].hx = hx;
				nstk++;
				return sstk[off].ob;
			}
		}
	}
	return 0U;
}

const char*
echs_zone(echs_tzob_t z)
{
	hash_t hx;
	hash_t k;
	size_t o;

	if (UNLIKELY(z == 0UL)) {
		return "UTC";
	} else if (UNLIKELY(!(k = hx = retr_tzob(z)))) {
		/* huh? */
		return NULL;
	}

	/* here's the initial probe then */
	for (size_t j = 0U; j < 9U; j++, k >>= 3U) {
		const size_t off = k & 0xffU;

		if (sstk[off].hx == hx) {
			/* found him (or super-collision) */
			o = sstk[off].of;
			goto yep;
		}
	}

	for (size_t i = SSTK_NSLOT, m = 0x3ffU; i < zstk;
	     i <<= 2U, m <<= 2U, m |= 3U) {
		/* reset k */
		k = hx;

		/* here we probe within the top entries of the stack */
		for (size_t j = 0U; j < 9U; j++, k >>= 3U) {
			const size_t off = (i | k) & m;

			if (sstk[off].hx == hx) {
				/* found him (or super-collision) */
				o = sstk[off].of;
				goto yep;
			}
		}
	}
	return NULL;

yep:
	return obs + o;
}

void
clear_tzobs(void)
{
	if (LIKELY(sstk != NULL)) {
		free(sstk);
	}
	sstk = NULL;
	zstk = 0U;
	nstk = 0U;
	if (LIKELY(obs != NULL)) {
		free(obs);
	}
	obs = NULL;
	obz = 0U;
	obn = 0U;

	/* clear MFU cache */
	for (size_t i = 0U; i < countof(zmfu); i++) {
		if (UNLIKELY(zmfu[i] == NULL)) {
			break;
		}
		zif_close(zmfu[i]);
	}
	memset(tmfu, 0, sizeof(tmfu));
	memset(zmfu, 0, sizeof(zmfu));
	return;
}

echs_instant_t
echs_instant_utc(echs_instant_t i, echs_tzob_t zob)
{
	zif_t z;

	i = echs_instant_detach_tzob(i);
	if (UNLIKELY(echs_instant_all_day_p(i))) {
		/* just do fuckall */
		;
	} else if (LIKELY((z = __tzob_zif(zob)) != NULL)) {
		time_t loc = __inst_to_epoch(i);
		time_t nix = zif_utc_time(z, loc);
		echs_idiff_t d = {.d = 1000 * (nix - loc)};

		i = echs_instant_add(i, d);
	}
	return i;
}

int
echs_tzob_offs(echs_tzob_t z, echs_instant_t i, int x)
{
	time_t nix;
	zif_t _z;

	i = echs_instant_detach_tzob(i);
	if (UNLIKELY(echs_instant_all_day_p(i))) {
		/* nah, all-day shit isn't offset at all */
		return 0;
	} else if (UNLIKELY((_z = __tzob_zif(z)) == NULL)) {
		return 0;
	}
	/* just convert the instant now */
	nix = __inst_to_epoch(i);
	return zif_find_zrng(_z, nix + x).offs;
}

/* tzob.c ends here */
