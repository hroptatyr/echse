/*** evstrm.c -- streams of events
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
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "evstrm.h"
#include "nifty.h"


/* our event class */
struct evmux_s {
	echs_evstrm_class_t class;
	/** number of streams */
	size_t ns;
	/** all them streams */
	echs_evstrm_t *s;
	/** event cache */
	echs_event_t ev[];
};

static echs_event_t next_evmux(echs_evstrm_t, bool popp);
static void free_evmux(echs_evstrm_t);
static echs_evstrm_t clone_evmux(echs_const_evstrm_t);
static void seria_evmux(int, echs_const_evstrm_t);

static const struct echs_evstrm_class_s evmux_cls = {
	.next = next_evmux,
	.free = free_evmux,
	.clone = clone_evmux,
	.seria = seria_evmux,
};

static void
seria_evmux(int whither, echs_const_evstrm_t strm)
{
	const struct evmux_s *this = (const struct evmux_s*)strm;

	for (size_t i = 0UL; i < this->ns; i++) {
		echs_evstrm_seria(whither, this->s[i]);
	}
	return;
}

static echs_event_t
next_evmux(echs_evstrm_t strm, bool popp)
{
	static const echs_event_t nul = {0U};
	struct evmux_s *this = (struct evmux_s*)strm;
	echs_event_t best;
	size_t besti;
	size_t i;

	if (UNLIKELY(this->s == NULL)) {
		return (echs_event_t){0};
	} else if (UNLIKELY(echs_max_instant_p(this->ev[0].from))) {
		/* precache events, the max instant in ev[0] is the indicator
		 * regardless of POPP we prefill without popping */
		for (size_t j = 0UL; j < this->ns; j++) {
			echs_evstrm_t s = this->s[j];
			this->ev[j] = echs_evstrm_next(s);
		}
	}
	/* best event so-far is the first non-null event */
	for (i = 0U; i < this->ns; i++) {
		best = this->ev[i];

		if (!echs_event_0_p(best)) {
			besti = i;
			break;
		}
	}
	/* quick check if we hit the event boundary */
	if (UNLIKELY(i >= this->ns)) {
		/* yep, bugger off free the streams and the stream array here */
		for (size_t j = 0U; j < this->ns; j++) {
			free_echs_evstrm(this->s[j]);
		}
		free(this->s);
		this->s = NULL;
		return nul;
	}
	/* otherwise try and find an earlier event */
	for (i++; i < this->ns; i++) {
		echs_event_t ecur = this->ev[i];

		if (echs_event_0_p(ecur)) {
			continue;
		} else if (echs_event_lt_p(ecur, best)) {
			best = ecur;
			besti = i;
		} else if (echs_event_eq_p(ecur, best)) {
			/* should this be optional? --uniq? */
			echs_evstrm_t s = this->s[i];
			(void)echs_evstrm_pop(s);
			this->ev[i] = echs_evstrm_next(s);
		}
	}
	/* return best but refill besti in popping mode */
	if (popp) {
		echs_evstrm_t s = this->s[besti];
		(void)echs_evstrm_pop(s);
		this->ev[besti] = echs_evstrm_next(s);
	}
	return best;
}

static echs_evstrm_t
make_evmux(echs_evstrm_t s[], size_t ns)
{
	struct evmux_s *res;

	/* slight optimisation here */
	if (UNLIKELY(ns == 0UL)) {
		res = NULL;
		goto trivial;
	} else if (UNLIKELY(ns == 1UL)) {
		res = (void*)*s;
		goto trivial;
	}
	/* otherwise we have to resort to merge-sorting aka muxing */
	res = malloc(sizeof(*res) + ns * sizeof(*res->ev));
	if (UNLIKELY(res == NULL)) {
		return NULL;
	}
	res->class = &evmux_cls;
	res->ns = ns;
	res->s = s;
	/* we used to precache events here but seeing as not every
	 * echse command would need the unrolled stream we leave it
	 * to next_evmux(), put an indicator into the event cache here */
	res->ev[0].from = echs_max_instant();
	return (echs_evstrm_t)res;

trivial:
	/* second exit, freeing the array passed on to us */
	free(s);
	return (echs_evstrm_t)res;
}

static void
free_evmux(echs_evstrm_t s)
{
	struct evmux_s *this = (struct evmux_s*)s;

	if (LIKELY(this->s != NULL)) {
		for (size_t i = 0; i < this->ns; i++) {
			if (LIKELY(this->s[i] != NULL)) {
				free_echs_evstrm(this->s[i]);
			}
		}
		free(this->s);
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evmux(echs_const_evstrm_t s)
{
	const struct evmux_s *this = (const struct evmux_s*)s;
	struct evmux_s *res;

	if (UNLIKELY(this->s == NULL)) {
		return NULL;
	}
	with (size_t z = this->ns * sizeof(*this->ev) + sizeof(*this)) {
		res = malloc(z);
		if (UNLIKELY(res == NULL)) {
			return NULL;
		}
		memcpy(res, this, z);
	}
	/* clone all streams */
	res->s = malloc(this->ns * sizeof(*this->s));
	if (UNLIKELY(res->s == NULL)) {
		free(res);
		return NULL;
	}
	for (size_t i = 0U; i < this->ns; i++) {
		echs_evstrm_t stmp;

		if (UNLIKELY((stmp = this->s[i]) == NULL)) {
			;
		} else if (UNLIKELY((stmp = clone_echs_evstrm(stmp)) == NULL)) {
			;
		}
		res->s[i] = stmp;
	}
	return (echs_evstrm_t)res;
}


echs_evstrm_t
echs_evstrm_mux(echs_evstrm_t s, ...)
{
	va_list ap;
	size_t nstrm = 0U;
	echs_evstrm_t *strm;
	size_t allocz;

	if (UNLIKELY(s == NULL)) {
		return NULL;
	}
	/* otherwise we've got at least 1 argument */
	strm = malloc((allocz = 16U) + sizeof(*strm));
	if (UNLIKELY(strm == NULL)) {
		return NULL;
	}
	va_start(ap, s);
	for (; s != NULL; s = va_arg(ap, echs_evstrm_t)) {
		if (nstrm >= allocz) {
			void *x = realloc(strm, (allocz *= 2) * sizeof(*strm));
			if (UNLIKELY(x == NULL)) {
				goto free;
			}
			strm = x;
		}
		strm[nstrm++] = clone_echs_evstrm(s);
	}
	va_end(ap);
	return make_evmux(strm, nstrm);
free:
	for (size_t i = 0U; i < nstrm; i++) {
		free(strm[i]);
	}
	free(strm);
	return NULL;
}

echs_evstrm_t
echs_evstrm_mux_clon(echs_evstrm_t s, ...)
{
	va_list ap;
	size_t nstrm = 0U;
	echs_evstrm_t *strm;
	size_t allocz;

	if (UNLIKELY(s == NULL)) {
		return NULL;
	}
	/* otherwise we've got at least 1 argument */
	strm = malloc((allocz = 16U) + sizeof(*strm));
	if (UNLIKELY(strm == NULL)) {
		return NULL;
	}
	va_start(ap, s);
	for (; s != NULL; s = va_arg(ap, echs_evstrm_t)) {
		if (nstrm >= allocz) {
			void *x = realloc(strm, (allocz *= 2) * sizeof(*strm));
			if (UNLIKELY(x == NULL)) {
				goto free;
			}
			strm = x;
		}
		strm[nstrm++] = s;
	}
	va_end(ap);
	return make_evmux(strm, nstrm);
free:
	for (size_t i = 0U; i < nstrm; i++) {
		free(strm[i]);
	}
	free(strm);
	return NULL;
}

echs_evstrm_t
echs_evstrm_vmux(const echs_evstrm_t s[], size_t n)
{
	echs_evstrm_t *strm;
	size_t nstrm = n;
	size_t strm1 = 0U;

	if (UNLIKELY(s == NULL)) {
		/* that was quick */
		return NULL;
	}

	for (size_t i = 0U; i < n; i++) {
		if (UNLIKELY(s[i] == NULL)) {
			nstrm--;
			if (UNLIKELY(i == strm1)) {
				strm1++;
			}
		}
	}

	if (UNLIKELY(nstrm == 0UL)) {
		return NULL;
	} else if (UNLIKELY(nstrm == 1UL)) {
		return s[strm1];
	}
	/* otherwise make a copy of S and then pass it to
	 * our make_evstrm(), it's the right signature already */
	strm = malloc(nstrm * sizeof(*strm));
	if (UNLIKELY(strm == NULL)) {
		return NULL;
	} else if (LIKELY(nstrm == n)) {
		memcpy(strm, s, n * sizeof(*s));
	} else {
		for (size_t i = strm1, j = 0U; i < n; i++) {
			if (s[i] != NULL) {
				strm[j++] = s[i];
			}
		}
	}
	return make_evmux(strm, nstrm);
}

echs_evstrm_t
echs_evstrm_vmux_clon(const echs_evstrm_t s[], size_t n)
{
	echs_evstrm_t *strm;
	size_t nstrm = n;
	size_t strm1 = 0U;

	if (UNLIKELY(s == NULL)) {
		/* that was quick */
		return NULL;
	}

	for (size_t i = 0U; i < n; i++) {
		if (UNLIKELY(s[i] == NULL)) {
			nstrm--;
			if (UNLIKELY(i == strm1)) {
				strm1++;
			}
		}
	}

	if (UNLIKELY(nstrm == 0UL)) {
		return NULL;
	} else if (UNLIKELY(nstrm == 1UL)) {
		return s[strm1];
	}
	/* otherwise make a copy of S and then pass it to
	 * our make_evstrm(), it's the right signature already */
	strm = malloc(nstrm * sizeof(*strm));
	if (UNLIKELY(strm == NULL)) {
		return NULL;
	}
	for (size_t i = strm1, j = 0U; i < n; i++) {
		if (UNLIKELY(s[i] == NULL)) {
			;
		} else if ((strm[j] = clone_echs_evstrm(s[i])) != NULL) {
			j++;
		}
	}
	return make_evmux(strm, nstrm);
}

size_t
echs_evstrm_demux(echs_evstrm_t *restrict tgt, size_t tsz,
		  const struct echs_evstrm_s *s, size_t offset)
{
	const struct evmux_s *this;
	size_t res = 0U;

	if (UNLIKELY(!(s->class == &evmux_cls))) {
		/* not our can of beer */
		return 0UL;
	}
	this = (const struct evmux_s*)s;
	for (size_t i = offset; i < this->ns && res < tsz; i++, res++) {
		tgt[res] = this->s[i];
	}
	return res;
}


/* file prober and ctor */
#include "evical.h"

echs_evstrm_t
make_echs_evstrm_from_file(const char *fn)
{
/* just try the usual readers for now,
 * DSO support and config files will come later */
	(void)fn;
	return NULL;
}

/* evstrm.c ends here */
