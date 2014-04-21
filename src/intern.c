/*** intern.c -- interning system
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "intern.h"
#include "nifty.h"

/* a hash is the bucket locator and a chksum for collision detection */
typedef struct {
	size_t idx;
	uint_fast32_t chk;
} hash_t;

/* the beef table */
static struct {
	obint_t ob;
	uint_fast32_t ck;
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

static hash_t
murmur(const uint8_t *str, size_t len)
{
/* tokyocabinet's hasher */
	size_t idx = 19780211U;
	uint_fast32_t hash = 751U;
	const uint8_t *rp = str + len;

	while (len--) {
		idx = idx * 37U + *str++;
		hash = (hash * 31U) ^ *--rp;
	}
	return (hash_t){idx, hash};
}

static inline size_t
get_off(size_t idx, size_t mod)
{
	/* no need to negate MOD as it's a 2-power */
	return -idx % mod;
}

static void*
recalloc(void *buf, size_t nmemb_ol, size_t nmemb_nu, size_t membz)
{
	nmemb_ol *= membz;
	nmemb_nu *= membz;
	buf = realloc(buf, nmemb_nu);
	memset((uint8_t*)buf + nmemb_ol, 0, nmemb_nu - nmemb_ol);
	return buf;
}

static obint_t
make_obint(const char *str, size_t len)
{
/* put STR (of length LEN) into string obarray, don't check for dups */
#define OBAR_MINZ	(1024U)
	/* make sure we pad with \0 bytes to the next 4-byte multiple */
	size_t pad = ((len / 4U) + 1U) * 4U;
	obint_t res;

	if (UNLIKELY(obn + pad >= obz)) {
		size_t nuz = (obz * 2U) ?: OBAR_MINZ;

		obs = recalloc(obs, obz, nuz, sizeof(*obs));
		obz = nuz;
	}
	/* paste the string in question */
	memcpy(obs + (res = obn), str, len);
	/* assemble the result */
	res >>= 2U;
	res |= len << ((sizeof(res) - 1U) * 8U);
	/* inc the obn pointer */
	obn += pad;
	return res;
}


obint_t
intern(const char *str, size_t len)
{
#define SSTK_MINZ	(256U)
#define OBINT_MAX_LEN	(256U)
	if (UNLIKELY(len == 0U || len >= OBINT_MAX_LEN)) {
		/* don't bother */
		return 0U;
	}
	for (const hash_t hx = murmur((const uint8_t*)str, len);;) {
		/* just try what we've got */
		for (size_t mod = SSTK_MINZ; mod <= zstk; mod *= 2U) {
			size_t off = get_off(hx.idx, mod);

			if (LIKELY(sstk[off].ck == hx.chk)) {
				/* found him */
				return sstk[off].ob;
			} else if (sstk[off].ob == 0U) {
				/* found empty slot */
				obint_t ob = make_obint(str, len);
				sstk[off].ob = ob;
				sstk[off].ck = hx.chk;
				nstk++;
				return ob;
			}
		}
		/* quite a lot of collisions, resize then */
		with (size_t nu = (zstk * 2U) ?: SSTK_MINZ) {
			sstk = recalloc(sstk, zstk, nu, sizeof(*sstk));
			zstk = nu;
		}
	}
	/* not reached */
}

void
unintern(obint_t UNUSED(ob))
{
	return;
}

const char*
obint_name(obint_t ob)
{
	if (UNLIKELY(ob == 0UL)) {
		return NULL;
	}
	return obs + obint_off(ob);
}

void
clear_interns(void)
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
	return;
}

/* intern.c ends here */
