/*** state.c -- state interning system
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "boobs.h"
#include "state.h"
#include "hash.h"
#include "nifty.h"

/* the beef table, seeing as we've got only 64 states to hand out,
 * just make the hash table twice as big and never resize */
static struct {
	echs_state_t st;
	hash_t hx;
} sstk[128U];
/* number of elements */
static size_t nstk;

/* the big string obarray */
static char *restrict sts;
/* alloc size, 2-power */
static size_t stz;
/* state -> offset mapping */
static size_t sto[sizeof(echs_stset_t) * 8U];
/* next st */
static size_t sti;


static void*
recalloc(void *buf, size_t nmemb_ol, size_t nmemb_nu, size_t membz)
{
	nmemb_ol *= membz;
	nmemb_nu *= membz;
	with (void *tmp = realloc(buf, nmemb_nu)) {
		if (UNLIKELY(tmp == NULL)) {
			free(buf);
			return NULL;
		}
		buf = tmp;
	}
	memset((uint8_t*)buf + nmemb_ol, 0, nmemb_nu - nmemb_ol);
	return buf;
}

static echs_state_t
make_state(const char *str, size_t len)
{
/* put STR (of length LEN) into string obarray, don't check for dups */
#define STAR_MINZ	(1024U)
	/* make sure we pad with \0 bytes to the next 4-byte multiple */
	size_t pad = ((len / 4U) + 1U) * 4U;

	if (UNLIKELY(sti >= countof(sto))) {
		return 0U;
	} else if (UNLIKELY(sto[sti] + pad >= stz)) {
		size_t nuz = (stz * 2U) ?: STAR_MINZ;

		sts = recalloc(sts, stz, nuz, sizeof(*sts));
		stz = nuz;
	}
	/* paste the string in question */
	memcpy(sts + sto[sti], str, len);
	/* inc the sto pointer */
	if (LIKELY(sti + 1U < countof(sto))) {
		sto[sti + 1U] = sto[sti] + pad;
	}
	return (echs_state_t)++sti;
}


#define SSTK_MINZ	(256U)
#define STATE_MAX_LEN	(256U)

echs_state_t
add_state(const char *str, size_t len)
{
	if (UNLIKELY(len == 0U || len >= STATE_MAX_LEN)) {
		/* don't bother */
		return 0U;
	}
	with (const hash_t hx = hash(str, len)) {
		const size_t off = hx % countof(sstk);

		if (LIKELY(sstk[off].hx == hx)) {
			/* found him (or super-collision) */
			return sstk[off].st;
		} else if (!sstk[off].hx) {
			/* found empty slot */
			echs_state_t st = make_state(str, len);
			sstk[off].st = st;
			sstk[off].hx = hx;
			nstk++;
			return st;
		}
	}
	return 0U;
}

echs_state_t
get_state(const char *str, size_t len)
{
	if (UNLIKELY(len == 0U || len >= STATE_MAX_LEN)) {
		/* don't bother */
		return 0U;
	}
	/* just try what we've got */
	with (const hash_t hx = hash(str, len)) {
		const size_t off = hx % countof(sstk);

		if (LIKELY(sstk[off].hx == hx)) {
			/* found him */
			return sstk[off].st;
		}
	}
	/* couldn't find him */
	return 0U;
}

const char*
state_name(echs_state_t st)
{
	if (UNLIKELY(!st || st >= sizeof(echs_stset_t) * 8U)) {
		return NULL;
	}
	return sts + sto[--st];
}

void
clear_states(void)
{
	memset(sstk, 0, sizeof(sstk));
	nstk = 0U;
	if (LIKELY(sts != NULL)) {
		free(sts);
	}
	sts = NULL;
	stz = 0U;
	memset(sto, 0, sizeof(sto));
	sti = 0U;
	return;
}

/* state.c ends here */
