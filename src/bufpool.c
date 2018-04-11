/*** bufpool.c -- pooling buffers
 *
 * Copyright (C) 2013-2018 Sebastian Freundt
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
#include "bufpool.h"
#include "nifty.h"

/* the big string obarray */
static struct bufpool_s *restrict obs;
static size_t nobs;
static size_t zobs;

static bufpool_t nul = {""};


bufpool_t
bufpool(const char *str, size_t len)
{
	void *tmp;
	size_t i;

	/* see if it's a sensible request first */
	if (UNLIKELY(len == 0UL)) {
		return nul;
	}
	if (UNLIKELY(nobs >= zobs)) {
		/* time to resize */
		tmp = realloc(obs, (zobs + 64U) * sizeof(*obs));

		if (UNLIKELY(tmp == NULL)) {
			/* retry next time */
			return nul;
		}
		/* otherwise consider us successful */
		obs = tmp;
		zobs += 64U;
	}
	if (UNLIKELY((tmp = malloc(len + 1U)) == NULL)) {
		/* bad luck again */
		return nul;
	}
	obs[nobs].str = tmp;
	obs[nobs].len = len;
	memcpy(obs[nobs].str, str, len);
	obs[nobs].str[len] = '\0';
	/* advance pointer and assemble result */
	i = nobs++;
	return (bufpool_t)obs[i];
}

void
bufunpool(bufpool_t bp)
{
	for (size_t i = 0U; i < nobs; i++) {
		if (bp.str == obs[i].str) {
			/* must have found him */
			free(obs[i].str);
			obs[i] = (bufpool_t){NULL, 0UL};
			break;
		}
	}
	return;
}

void
clear_bufpool(void)
{
	if (LIKELY(obs != NULL)) {
		for (size_t i = 0U; i < nobs; i++) {
			if (LIKELY(obs[i].str != NULL)) {
				free(obs[i].str);
			}
		}
		free(obs);
	}
	obs = NULL;
	zobs = 0U;
	nobs = 0U;
	return;
}

/* bufpool.c ends here */
