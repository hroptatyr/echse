/*** strlst.c -- list of NULL terminated strings a la env
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
#include <stdint.h>
#include <string.h>
#include "strlst.h"
#include "nifty.h"

static __attribute__((const, pure)) size_t
ilog_ceil_exp(size_t n)
{
/* return the next 2-power > N, at least 16 though */
	n |= 0b1111U;
	n |= n >> 1U;
	n |= n >> 2U;
	n |= n >> 4U;
	n |= n >> 8U;
	n |= n >> 16U;
	n |= n >> 32U;
	n++;
	return n;
}

struct strlst_s*
clone_strlst(const struct strlst_s *sl)
{
	const size_t z = ilog_ceil_exp(sl->i);
	const size_t zl = ilog_ceil_exp(sl->nl + 1U);
	struct strlst_s *res;

	if ((res = malloc(sizeof(*sl) + zl * sizeof(*sl->l))) == NULL) {
		return NULL;
	} else if (UNLIKELY((res->s = malloc(z)) == NULL)) {
		free(res);
		return NULL;
	}
	memcpy(res->s, sl->s, sl->i * sizeof(*sl->s));
	res->i = sl->i;
	res->nl = sl->nl;
	/* and finally the pointer-to-string list */
	memcpy(res->l, sl->l, (sl->nl + 1U) * sizeof(*sl->l));
	with (ptrdiff_t d = res->s - sl->s) {
		for (size_t i = 0U; res->l[i]; i++) {
			res->l[i] += d;
		}
	}
	return res;
}

void
strlst_addn(struct strlst_s *sl[static 1U], const char *s, size_t n)
{
	struct strlst_s *res;
	size_t z;

	if (UNLIKELY((res = *sl) == NULL)) {
		z = ilog_ceil_exp(n + 1U);
		res = malloc(sizeof(*res) +  16U * sizeof(*res->l));
		if (UNLIKELY(res == NULL)) {
			/* bollocks */
			return;
		} else if (UNLIKELY((res->s = malloc(z)) == NULL)) {
			/* brilliant */
			free(res);
			return;
		}
		res->i = 0U;
		res->nl = 0U;
	} else if (z = ilog_ceil_exp(res->i), UNLIKELY(res->i + n + 1U > z)) {
		/* realloc the string pool */
		const size_t nu_z = ilog_ceil_exp(res->i + n + 1U);
		char *nu_s;

		nu_s = realloc(res->s, nu_z * sizeof(*res->s));
		if (UNLIKELY(nu_s == NULL)) {
			goto err;
		}
		/* adjust existing pointers */
		with (ptrdiff_t d = nu_s - res->s) {
			for (size_t i = 0U; res->l[i]; i++) {
				res->l[i] += d;
			}
		}
		res->s = nu_s;
	}

	const size_t zl = ilog_ceil_exp(res->nl);
	if (UNLIKELY(res->nl + 1U >= zl)) {
		/* resize everything */
		void *tmp;

		tmp = realloc(res, sizeof(*res) + 2U * zl * sizeof(*res->l));
		if (UNLIKELY(tmp == NULL)) {
			/* completely cunted */
			goto err;
		}
	}
	/* now it's time for beef */
	res->l[res->nl++] = res->s + res->i;
	res->l[res->nl] = NULL;
	/* and dessert */
	memcpy(res->s + res->i, s, n);
	res->i += n;
	res->s[res->i++] = '\0';
	*sl = res;
	return;

err:
	free(res->s);
	free(res);
	*sl = NULL;
	return;
}

void
strlst_add(struct strlst_s *sl[static 1U], const char *s)
{
	strlst_addn(sl, s, strlen(s));
	return;
}

void
free_strlst(struct strlst_s *sl)
{
	if (UNLIKELY(sl == NULL)) {
		return;
	}
	free(sl->s);
	free(sl);
	return;
}

/* strlst.c ends here */
