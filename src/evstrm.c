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

static echs_event_t next_evmux(echs_evstrm_t);
static void free_evmux(echs_evstrm_t);
static echs_evstrm_t clone_evmux(echs_evstrm_t);
static void prnt_evmux1(echs_evstrm_t);
static void prnt_evmuxm(echs_evstrm_t);

static const struct echs_evstrm_class_s evmux_cls = {
	.next = next_evmux,
	.free = free_evmux,
	.clone = clone_evmux,
	.prnt1 = prnt_evmux1,
};

static const struct echs_evstrm_class_s evmuxm_cls = {
	.next = next_evmux,
	.free = free_evmux,
	.clone = clone_evmux,
	.prnt1 = prnt_evmuxm,
};

static __attribute__((nonnull(1))) void
__refill(struct evmux_s *this, size_t idx)
{
	echs_evstrm_t s = this->s[idx];

	if (UNLIKELY(echs_event_0_p(this->ev[idx] = echs_evstrm_next(s)))) {
		free_echs_evstrm(s);
		this->s[idx] = NULL;
	}
	return;
}

static echs_event_t
next_evmux(echs_evstrm_t strm)
{
	static const echs_event_t nul = {0U};
	struct evmux_s *this = (struct evmux_s*)strm;
	echs_event_t best;
	size_t besti;
	size_t i;

	if (UNLIKELY(this->s == NULL)) {
		return (echs_event_t){0};
	} else if (UNLIKELY(echs_max_instant_p(this->ev[0].from))) {
		/* precache events, the max instant in ev[0] is the indicator */
		for (size_t j = 0UL; j < this->ns; j++) {
			__refill(this, j);
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
		/* yep, bugger off, we assume the streams have been
		 * freed upon __refill() previously, so just free the
		 * stream array here */
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
			__refill(this, i);
		}
	}
	/* return best but refill besti */
	__refill(this, besti);
	return best;
}

static void
prnt_evmux1(echs_evstrm_t strm)
{
	const struct evmux_s *this = (struct evmux_s*)strm;

	for (size_t i = 0UL; i < this->ns; i++) {
		echs_evstrm_prnt(this->s[i]);
	}
	return;
}

static void
prnt_evmuxm(echs_evstrm_t strm)
{
	const struct evmux_s *this = (struct evmux_s*)strm;

	this->s[0]->class->prntm(this->s, this->ns);
	return;
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
	res->class = &evmux_cls;
	res->ns = ns;
	res->s = s;
	/* check if we can use the many-items printer */
	if ((*s)->class->prntm != NULL) {
		const echs_evstrm_class_t proto = (*s)->class;
		bool same_class_p = true;

		for (size_t i = 1U; i < ns; i++) {
			if (s[i]->class != proto) {
				same_class_p = false;
				break;
			}
		}
		if (same_class_p) {
			res->class = &evmuxm_cls;
		}
	}
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
			if (UNLIKELY(this->s[i] != NULL)) {
				free_echs_evstrm(this->s[i]);
			}
		}
		free(this->s);
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evmux(echs_evstrm_t s)
{
	struct evmux_s *this = (struct evmux_s*)s;
	struct evmux_s *res;

	if (UNLIKELY(this->s == NULL)) {
		return NULL;
	}
	with (size_t z = this->ns * sizeof(*this->ev) + sizeof(*this)) {
		res = malloc(z);
		memcpy(res, this, z);
	}
	/* clone all streams */
	res->s = malloc(this->ns * sizeof(*this->s));
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
	va_start(ap, s);
	for (; s != NULL; s = va_arg(ap, echs_evstrm_t)) {
		if (nstrm >= allocz) {
			strm = realloc(strm, (allocz *= 2) * sizeof(*strm));
		}
		strm[nstrm++] = clone_echs_evstrm(s);
	}
	va_end(ap);
	return make_evmux(strm, nstrm);
}

echs_evstrm_t
echs_evstrm_vmux(const echs_evstrm_t s[], size_t n)
{
	echs_evstrm_t *strm;
	size_t nstrm = 0UL;

	if (UNLIKELY(s == NULL || n == 0UL)) {
		return NULL;
	}
	/* otherwise make a copy of S and then pass it to
	 * our make_evstrm(), it's the right signature already */
	strm = malloc(n * sizeof(*strm));
	for (size_t i = 0U; i < n; i++) {
		echs_evstrm_t stmp;

		if (UNLIKELY(s[i] == NULL)) {
			;
		} else if (UNLIKELY((stmp = clone_echs_evstrm(s[i])) == NULL)) {
			;
		} else {
			strm[nstrm++] = stmp;
		}
	}
	return make_evmux(strm, nstrm);
}

echs_evstrm_t
make_echs_evmux(echs_evstrm_t s[], size_t n)
{
	echs_evstrm_t *strm;
	size_t nstrm = n;

	if (UNLIKELY(s == NULL || n == 0UL)) {
		return NULL;
	} else if (UNLIKELY(n == 1UL)) {
		return *s;
	}
	/* otherwise make a copy of S and then pass it to
	 * our make_evstrm(), it's the right signature already */
	strm = malloc(n * sizeof(*strm));
	memcpy(strm, s, n * sizeof(*s));
	return make_evmux(strm, nstrm);
}


/* file prober and ctor */
#include "evical.h"

echs_evstrm_t
make_echs_evstrm_from_file(const char *fn)
{
/* just try the usual readers for now,
 * DSO support and config files will come later */
	echs_evstrm_t s;

	if (0) {
		;
	} else if ((s = make_echs_evical(fn)) != NULL) {
		;
	}
	return s;
}

/* evstrm.c ends here */