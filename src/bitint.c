/*** bitint.c -- integer degrading bitsets
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
#include <string.h>
#include "bitint.h"
#include "nifty.h"


static void
ass_bs(bitint383_t *restrict bi, int x)
{
	if (x > 0) {
		unsigned int p = (unsigned int)x % 32U;
		unsigned int j = (unsigned int)x / 32U;

		bi->pos[j] |= 1U << p;
	} else {
		unsigned int p = (unsigned int)(-x) % 32U;
		unsigned int j = (unsigned int)(-x) / 32U;
		bi->neg[j] |= 1U << p;
	}
	return;
}

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
	} else if ((i = *bi->pos >> 1U) < countof(bi->pos)) {
		/* i is our candidate now, yay */
		/* just store it here and get on with life
		 * check for dupes though */
		for (size_t j = 0U; j < i; j++) {
			if (UNLIKELY(bi->neg[j] == x)) {
				/* dupe, bugger off */
				return;
			}
		}
		*bi->pos += 2U;
		bi->neg[i] = x;
		return;
	}
	/* otherwise, bi needs degrading, copy to tmp first */
	memcpy(tmp, bi->neg, sizeof(tmp));
	memset(bi, 0, sizeof(*bi));
	*bi->pos = 1U;
	for (i = 0U; i < countof(bi->pos); i++) {
		ass_bs(bi, tmp[i]);
	}
bitset:
	/* when we're here, everything is degraded */
	ass_bs(bi, x);
	return;
}

int
bi383_next(bitint_iter_t *restrict iter, const bitint383_t *bi)
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
	} else if (*iter < countof(bi->pos) * 32U) {
		/* positives */
		uint32_t tmp;

		ij = *iter / 32U;
		ip = *iter % 32U;

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
			res = ij * 32U + ip;
			*iter = res + 1U;
		}
	} else if (*iter > countof(bi->pos) * 32U) {
		/* negatives */
		uint32_t tmp;

		ij = *iter / 32U;
		ip = *iter % 32U;
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
			res = -(32U * ij + ip);
			*iter = -res + 32U * countof(bi->pos) + 1U;
		}
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

/* bitint.c ends here */
