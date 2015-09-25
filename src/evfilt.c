/*** evfilt.c -- filtering streams by events from other streams
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
#include <stdlib.h>
#include <stdbool.h>
#include "evfilt.h"
#include "range.h"
#include "nifty.h"

/* generic stream with exceptions */
struct evfilt_s {
	echs_evstrm_class_t class;

	/* normal events */
	echs_evstrm_t e;
	/* exception events */
	echs_evstrm_t x;
	/* the next exception */
	echs_range_t ex;

	/* more exceptions for later addition,
	 * can be extended beyond the initial 16 */
	size_t nexs;
	echs_range_t exs[16U];
};


static echs_event_t next_evfilt(echs_evstrm_t, bool);
static void free_evfilt(echs_evstrm_t);
static echs_evstrm_t clone_evfilt(echs_const_evstrm_t);
static void send_evfilt(int whither, echs_const_evstrm_t s);

static const struct echs_evstrm_class_s evfilt_cls = {
	.next = next_evfilt,
	.free = free_evfilt,
	.clone = clone_evfilt,
	.seria = send_evfilt,
};

static echs_event_t
next_evfilt(echs_evstrm_t s, bool popp)
{
	struct evfilt_s *this = (struct evfilt_s*)s;
	echs_event_t e = echs_evstrm_next(this->e, popp);

check:
	if (UNLIKELY(echs_nul_range_p(this->ex))) {
		/* no more exceptions */
		return e;
	}

	/* otherwise check if the current exception overlaps with E */
	with (echs_range_t r = echs_event_range(e)) {
		if (echs_range_overlaps_p(r, this->ex)) {
			/* yes it does */
			e = echs_evstrm_next(this->e, true);
			goto check;
		} else if (echs_range_precedes_p(this->ex, r)) {
			/* we can't say for sure yet as there could be
			 * another exception in the range of E */
			echs_event_t ex = echs_evstrm_next(this->x, true);
			this->ex = echs_event_range(ex);
			goto check;
		}
	}
	/* otherwise it's certainly safe */
	return e;
}

static void
free_evfilt(echs_evstrm_t s)
{
	struct evfilt_s *this = (struct evfilt_s*)s;

	free_echs_evstrm(this->e);
	if (LIKELY(this->x != NULL)) {
		free_echs_evstrm(this->x);
	}
	return;
}

static echs_evstrm_t
clone_evfilt(echs_const_evstrm_t s)
{
	const struct evfilt_s *that = (const struct evfilt_s*)s;
	struct evfilt_s *this;

	if (UNLIKELY((this = malloc(sizeof(*this))) == NULL)) {
		return NULL;
	}
	this->class = &evfilt_cls;
	this->e = clone_echs_evstrm(that->e);
	this->x = clone_echs_evstrm(that->x);
	this->ex = that->ex;
	return (echs_evstrm_t)this;
}

static void
send_evfilt(int whither, echs_const_evstrm_t s)
{
	const struct evfilt_s *this = (const struct evfilt_s*)s;

	echs_evstrm_seria(whither, this->e);
	return;
}


echs_evstrm_t
make_evfilt(echs_evstrm_t e, echs_evstrm_t x)
{
	struct evfilt_s *res;

	if (UNLIKELY(e == NULL)) {
		/* filtering out no events will result in no events */
		return NULL;
	} else if (UNLIKELY(x == NULL)) {
		/* a filter that doesn't filter stuff adds nothing */
		return e;
	} else if (UNLIKELY((res = malloc(sizeof(*res))) == NULL)) {
		return NULL;
	}
	res->class = &evfilt_cls;
	res->e = e;
	res->x = x;
	with (echs_event_t nx = echs_evstrm_next(x, true)) {
		res->ex = echs_event_range(nx);
	}
	return (echs_evstrm_t)res;
}

/* evfilt.c ends here */
