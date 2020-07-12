/*** bitint.c -- integer degrading bitsets
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
#include <string.h>
#include "bitint.h"
#include "nifty.h"

#define POS_BITZ	(sizeof(*(bitint383_t){}.pos) * 8U)
#define NEG_BITZ	(sizeof(*(bitint383_t){}.neg) * 8U)


#define bitint_t	bitint383_t
#define ass_bs		ass_bs383
#define ass_int		ass_int383
#include "bitint-bobs.c"

#define bitint_t	bitint447_t
#define ass_bs		ass_bs447
#define ass_int		ass_int447
#include "bitint-bobs.c"


void
ass_bi383(bitint383_t *restrict bi, int x)
{
/* LSB set in pos[0U]: bitset
 * otherwise native
 * if native storage, *bi->pos >> 1U will hold the number of ints
 * stored so far */
	static int32_t tmp[countof(bi->pos)];
	size_t i;

	/* maybe it's natively stored */
	if (*bi->pos & 0b1U) {
		goto bitset;
	} else if (*bi->pos >> 1U < countof(bi->pos)) {
		ass_int383(bi, x);
		return;
	}
	/* otherwise, bi needs degrading, copy to tmp first */
	memcpy(tmp, bi->neg, sizeof(tmp));
	memset(bi, 0, sizeof(*bi));
	*bi->pos = 1U;
	for (i = 0U; i < countof(bi->pos); i++) {
		ass_bs383(bi, tmp[i]);
	}
bitset:
	/* when we're here, everything is degraded */
	ass_bs383(bi, x);
	return;
}

int
bi383_next(bitint_iter_t *restrict iter, const bitint383_t *bi)
{
	size_t ij;
	size_t ip;
	int res;

	if (!(*bi->pos & 0b1U)) {
		/* native */
		if (UNLIKELY(*iter >= *bi->pos >> 1U)) {
			goto term;
		}
		res = bi->neg[(*iter)++];
	} else if (!*iter && ((*iter)++, *bi->neg & 0b1U)) {
		/* get the naught out now,
		 * or at least advance *iter so that the code below works */
		res = 0U;
	} else if (*iter < countof(bi->pos) * POS_BITZ) {
		/* positives */
		uint32_t tmp;

		ij = *iter / POS_BITZ;
		ip = *iter % POS_BITZ;

		if (tmp = bi->pos[ij], tmp >>= ip) {
			/* found him already */
			;
		} else for (ij++, ip = 0U; ij < countof(bi->pos); ij++) {
			if ((tmp = bi->pos[ij])) {
				/* good enough */
				break;
			}
		}
		if (ij >= countof(bi->pos)) {
			/* to no avail, switch to negs */
			ij = 0U, ip = 1U;
			goto negs;
		} else {
			for (; !(tmp & 0b1U); ip++, tmp >>= 1U);
			res = ij * POS_BITZ + ip;
			*iter = res + 1U;
		}
	} else if (*iter > countof(bi->pos) * POS_BITZ &&
		   *iter < countof(bi->neg) * NEG_BITZ +
		   countof(bi->pos) * POS_BITZ) {
		/* negatives */
		uint32_t tmp;

		ij = *iter / POS_BITZ;
		ip = *iter % POS_BITZ;
		ij -= countof(bi->pos);

	negs:
		if (tmp = bi->neg[ij], tmp >>= ip) {
			/* yay */
			;
		} else for (ij++, ip = 0U; ij < countof(bi->pos); ij++) {
			if ((tmp = bi->neg[ij])) {
				/* good enough */
				break;
			}
		}
		if (ij >= countof(bi->pos)) {
			/* we're simply out of luck */
			goto term;
		} else {
			for (; !(tmp & 0b1U); ip++, tmp >>= 1U);
			res = -(POS_BITZ * ij + ip);
			*iter = -res + POS_BITZ * countof(bi->pos) + 1U;
		}
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

int
bi383_max0(const bitint383_t *bi)
{
	int res = 0;

	if (!(*bi->pos & 0b1U)) {
		/* native */
		size_t n ;
		for (n = *bi->pos >> 1U; n > 0 && bi->neg[--n] < 0;);
		res = bi->neg[n];
		res = res > 0 ? res : 0;
	} else {
		/* positives */
		size_t i;

		for (i = countof(bi->pos); i > 0 && !bi->pos[--i];);
		with (uint32_t tmp = bi->pos[i]) {
			res = __builtin_clz(tmp) + (!!tmp);
		}
		res = (i + 1U) * POS_BITZ - res;
	}
	return res;
}

void
ass_bi447(bitint447_t *restrict bi, int x)
{
/* LSB set in pos[0U]: bitset
 * otherwise native
 * if native storage, *bi->pos >> 1U will hold the number of ints
 * stored so far */
	static int32_t tmp[countof(bi->pos)];
	size_t i;

	/* maybe it's natively stored */
	if (*bi->pos & 0b1U) {
		goto bitset;
	} else if (*bi->pos >> 1U < countof(bi->pos)) {
		ass_int447(bi, x);
		return;
	}
	/* otherwise, bi needs degrading, copy to tmp first */
	memcpy(tmp, bi->neg, sizeof(tmp));
	memset(bi, 0, sizeof(*bi));
	*bi->pos = 1U;
	for (i = 0U; i < countof(bi->pos); i++) {
		ass_bs447(bi, tmp[i]);
	}
bitset:
	/* when we're here, everything is degraded */
	ass_bs447(bi, x);
	return;
}

int
bi447_next(bitint_iter_t *restrict iter, const bitint447_t *bi)
{
	size_t ij;
	size_t ip;
	int res;

	if (!(*bi->pos & 0b1U)) {
		if (UNLIKELY(*iter >= *bi->pos >> 1U)) {
			goto term;
		}
		res = bi->neg[(*iter)++];
	} else if (!*iter && ((*iter)++, *bi->neg & 0b1U)) {
		/* get the naught out now,
		 * or at least advance *iter so that the code below works */
		res = 0U;
	} else if (*iter < countof(bi->pos) * POS_BITZ) {
		/* positives */
		uint32_t tmp;

		ij = *iter / POS_BITZ;
		ip = *iter % POS_BITZ;

		if (tmp = bi->pos[ij], tmp >>= ip) {
			/* found him already */
			;
		} else for (ij++, ip = 0U; ij < countof(bi->pos); ij++) {
			if ((tmp = bi->pos[ij])) {
				/* good enough */
				break;
			}
		}
		if (ij >= countof(bi->pos)) {
			/* to no avail, switch to negs */
			ij = 0U, ip = 1U;
			goto negs;
		} else {
			for (; !(tmp & 0b1U); ip++, tmp >>= 1U);
			res = ij * POS_BITZ + ip;
			*iter = res + 1U;
		}
	} else if (*iter > countof(bi->pos) * POS_BITZ &&
		   *iter < countof(bi->pos) * NEG_BITZ +
		   countof(bi->pos) * POS_BITZ) {
		/* negatives */
		uint32_t tmp;

		ij = *iter / POS_BITZ;
		ip = *iter % POS_BITZ;
		ij -= countof(bi->pos);

	negs:
		if (tmp = bi->neg[ij], tmp >>= ip) {
			/* yay */
			;
		} else for (ij++, ip = 0U; ij < countof(bi->pos); ij++) {
			if ((tmp = bi->neg[ij])) {
				/* good enough */
				break;
			}
		}
		if (ij >= countof(bi->pos)) {
			/* we're simply out of luck */
			goto term;
		} else {
			for (; !(tmp & 0b1U); ip++, tmp >>= 1U);
			res = -(POS_BITZ * ij + ip);
			*iter = -res + POS_BITZ * countof(bi->pos) + 1U;
		}
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

/* bitint.c ends here */
