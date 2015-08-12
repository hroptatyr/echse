/*** intern.c -- interning system
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "boobs.h"
#include "intern.h"
#include "nifty.h"

/* a hash is the bucket locator and a chksum for collision detection */
typedef uint_fast32_t hash_t;

/* the beef table */
static struct {
	obint_t ob;
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

static char
u2h(uint8_t c)
{
	switch (c) {
	case 0 ... 9:
		return (char)(c + '0');
	case 10 ... 15:
		return (char)(c + 'a' - 10);
	default:
		break;
	}
	return (char)'?';
}

static hash_t
murmur(const uint8_t *str, size_t len)
{
/* See http://murmurhash.googlepages.com/ for info about the Murmur hash.
 * Murmur is a non-cryptographic hashing hash  is by Austin Appleby. */
	const uint_fast32_t c1 = 0xcc9e2d51U;
	const uint_fast32_t c2 = 0x1b873593U;
	const size_t nb = len / 4U;
	const uint8_t *const tail = (const uint8_t*)(str + nb * 4U);
#if BYTE_ORDER == LITTLE_ENDIAN
	const uint32_t *b = (const uint32_t*)tail;
#endif	/* LITTLE_ENDIAN */
	hash_t h = 0U;
	hash_t k;

	for (ssize_t i = -nb; i; i++) {
#if BYTE_ORDER == LITTLE_ENDIAN
		k = b[i];
#elif BYTE_ORDER == BIG_ENDIAN
		k = 0U;
		k ^= tail[4 * i + 0] << 0U;
		k ^= tail[4 * i + 1] << 8U;
		k ^= tail[4 * i + 2] << 16U;
		k ^= tail[4 * i + 3] << 24U;
#else
# warning byte order detection failed, expect bogosity
#endif	/* BYTE_ORDERS */
		k *= c1;
		k &= 0xffffffffU;
		k = (k << 15U) ^ (k >> (32U - 15U));
		k *= c2;
		h ^= k;
		h &= 0xffffffffU;
		h = (h << 13U) ^ (h >> (32U - 13U));
		h = (h * 5U) + 0xe6546b64U;
	}
	/* reset k and process the tail */
	k = 0U;
	switch (nb % 4U) {
	case 3U:
		k ^= tail[2U] << 16U;
		/*@fallthrough@*/
	case 2U:
		k ^= tail[1U] << 8U;
		/*@fallthrough@*/
	case 1U:
		k ^= tail[0U] << 0U;
		k *= c1;
		k &= 0xffffffffU;
		k = (k << 15U) | (k >> (32U - 15U));
		k *= c2;
		h ^= k;
		/*@fallthrough@*/
	case 0U:
		break;
	}

	h ^= nb;
	h &= 0xffffffffU;
	h ^= h >> 16U;
	h *= 0x85ebca6bU;
	h &= 0xffffffffU;
	h ^= h >> 13U;
	h *= 0xc2b2ae35U;
	h &= 0xffffffffU;
	h ^= h >> 16U;
	return h;
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
		if (UNLIKELY(obs == NULL)) {
			obz = obn = 0U;
			return 0U;
		}
	}
	/* paste the string in question */
	memcpy(obs + (res = obn), str, len);
	/* assemble the result */
	res >>= 2U;
	res <<= 8U;
	res |= len;
	/* inc the obn pointer */
	obn += pad;
	return res;
}


obint_t
intern(const char *str, size_t len)
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
	const hash_t hx = murmur((const uint8_t*)str, len);
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
			return hx;
		} else if (!sstk[off].hx) {
			/* found empty slot */
			obint_t ob = make_obint(str, len);
			sstk[off].ob = ob;
			sstk[off].hx = hx;
			nstk++;
			return hx;
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
				return hx;
			} else if (!sstk[off].hx) {
				/* found empty slot */
				obint_t ob = make_obint(str, len);
				sstk[off].ob = ob;
				sstk[off].hx = hx;
				nstk++;
				return hx;
			}
		}
	}
	return 0U;
}

void
unintern(obint_t UNUSED(ob))
{
	return;
}

obint_t
obint(const char *str, size_t len)
{
/* like `intern()' but don't actually intern the string */
	const hash_t hx = murmur((const uint8_t*)str, len);
	return hx;
}

const char*
obint_name(obint_t hx)
{
	static char buf[32] = "echse/autouid-0x00000000@echse";
	hash_t k = hx;
	obint_t r;

	if (UNLIKELY(hx == 0UL)) {
		return obs;
	} else if (UNLIKELY(sstk == NULL)) {
		goto auto_uid;
	}

	/* here's the initial probe then */
	for (size_t j = 0U; j < 9U; j++, k >>= 3U) {
		const size_t off = k & 0xffU;

		if (sstk[off].hx == hx) {
			/* found him (or super-collision) */
			r = sstk[off].ob;
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
				r = sstk[off].ob;
				goto yep;
			}
		}
	}
auto_uid:
	/* it's probably one of those autogenerated uids */
	for (char *restrict bp = buf + 16U + 8U; bp > buf + 16U; hx >>= 4U) {
		*--bp = u2h((uint8_t)(hx & 0xfU));
	}
	return buf;

yep:
	return obs + ((r >> 8U) << 2U);
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
