/*** evmrul.c -- mover rules
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
#include <stdbool.h>
#include <string.h>
#include "evmrul.h"
#include "evstrm.h"
#include "nifty.h"

/* simple event queue */
struct eq_s {
	size_t i;
	echs_event_t e[16U];
};

/* mrul streams take an ordinary stream (the one with the movers) and
 * an auxiliary stream (the one with the states) and merge them into
 * an ordinary mover-free stream */
struct evmrul_s {
	echs_evstrm_class_t class;
	echs_evstrm_t movers;
	echs_evstrm_t states;
	mrulsp_t mr;
	/* aux event stack */
	struct eq_s aux[1U];
};

static inline void
push_event(struct eq_s *restrict q, echs_event_t e)
{
	q->e[q->i++] = e;
	return;
}

static inline echs_event_t
pop_event(struct eq_s *restrict q)
{
	if (UNLIKELY(q->i == 0U)) {
		return echs_nul_event();
	}
	return q->e[--q->i];
}

static inline __attribute__((const, pure)) bool
aux_blocks_p(mrulsp_t mr, echs_event_t aux)
{
	return mr.from & aux.sts;
}

static echs_event_t
pop_aux_event(struct evmrul_s *restrict s)
{
	echs_event_t res;

	if (!echs_nul_event_p(res = pop_event(s->aux))) {
		return res;
	} else if (UNLIKELY(s->states == NULL)) {
		return res;
	}
	return echs_evstrm_pop(s->states);
}

static void
push_aux_event(struct evmrul_s *restrict s, echs_event_t e)
{
	if (UNLIKELY(!aux_blocks_p(s->mr, e))) {
		return;
	}
	push_event(s->aux, e);
	return;
}


static echs_event_t next_evmrul_past(echs_evstrm_t, bool popp);
static echs_event_t next_evmrul_futu(echs_evstrm_t, bool popp);
static void free_evmrul(echs_evstrm_t);
static echs_evstrm_t clone_evmrul(echs_const_evstrm_t);
static void send_evmrul(int, echs_const_evstrm_t);

static const struct echs_evstrm_class_s evmrul_past_cls = {
	.next = next_evmrul_past,
	.free = free_evmrul,
	.clone = clone_evmrul,
	.seria = send_evmrul,
};

static const struct echs_evstrm_class_s evmrul_futu_cls = {
	.next = next_evmrul_futu,
	.free = free_evmrul,
	.clone = clone_evmrul,
	.seria = send_evmrul,
};

static echs_event_t
next_evmrul_past(echs_evstrm_t s, bool popp)
{
/* this is for past movers only at the moment
 * we take the next event from the to-be-moved stream (the movers) and
 * the current event from the auxiliary stream (the states) and if the
 * former is currently blocked by the latter, move it straight in front
 * of the states. */
	struct evmrul_s *restrict this = (struct evmrul_s*)s;
	echs_event_t res = echs_evstrm_next(this->movers);
	echs_event_t aux = pop_aux_event(this);
	echs_instant_t res_til = echs_instant_add(res.from, res.dur);
	echs_instant_t aux_til;

	if (UNLIKELY(echs_nul_event_p(aux))) {
		goto pop;
	} else if (echs_instant_le_p(res_til, aux.from)) {
		/* no danger then aye */
		goto out;
	}
	/* invariant: AUX.FROM < TIL */
	/* fast forward to the event that actually blocks RES */
	do {
		/* get next blocking event */
		echs_event_t nex = pop_aux_event(this);
		echs_idiff_t and;

		aux_til = echs_instant_add(aux.from, aux.dur);
		if (UNLIKELY(echs_nul_event_p(nex))) {
			/* state stream and event cache are finished,
			 * prepare to go to short-circuiting mode,
			 * RES will fit trivially between PREV_AUX and
			 * the most-distant event in the future */
			free_echs_evstrm(this->states);
			this->states = NULL;

			if (echs_instant_le_p(aux_til, res.from)) {
				/* the invariant before the loop does not
				 * guarantee that AUX_TIL <= RES.FROM */
				return res;
			}
			break;
		}

		and = echs_instant_diff(nex.from, aux_til);
		/* check if RES would fit between AUX and NEX */
		if (echs_idiff_le_p(res.dur, and)) {
			/* yep, would fit, forget about aux */
			if (!aux_blocks_p(this->mr, aux = nex)) {
				/* it's just noise though */
				goto out;
			}
		} else if (aux_blocks_p(this->mr, nex)) {
			/* no fittee, `extend' aux */
			aux_til = echs_instant_add(nex.from, nex.dur);
		}
	} while (echs_instant_le_p(aux_til, res.from));
	/* invariant RES.FROM < RES_TIL
	 * check if RES is entirely before AUX we're on our way */
	if (!echs_instant_le_p(res_til, aux.from)) {
		/* ah, we need to move RES just before AUX.FROM */
		res_til = aux.from;
		res.from = echs_instant_add(aux.from, echs_idiff_neg(res.dur));
	}
out:
	/* better save what we've got */
	push_aux_event(this, aux);
pop:
	if (popp) {
		(void)echs_evstrm_pop(this->movers);
	}
	return res;
}

static echs_event_t
next_evmrul_futu(echs_evstrm_t s, bool popp)
{
/* this is for future movers only at the moment.
 * Same idea as the past movers but move mover event into the future. */
	struct evmrul_s *restrict this = (struct evmrul_s*)s;
	echs_event_t res = echs_evstrm_next(this->movers);
	echs_event_t aux = pop_aux_event(this);
	echs_instant_t res_til = echs_instant_add(res.from, res.dur);
	echs_instant_t aux_til;

	if (UNLIKELY(echs_nul_event_p(aux))) {
		goto pop;
	}

	/* fast forward to the event that actually blocks RES */
	while (aux_til = echs_instant_add(aux.from, aux.dur),
	       echs_instant_le_p(aux_til, res.from)) {
		aux = pop_aux_event(this);
		if (UNLIKELY(echs_nul_event_p(aux))) {
			/* state stream and event cache are finished,
			 * prepare to go to short-circuiting mode,
			 * RES will fit trivially between PREV_AUX and
			 * the most-distant event in the future */
			free_echs_evstrm(this->states);
			this->states = NULL;
			return res;
		}
	}
	/* invariant: RES.FROM < AUX.TILL */
	if (echs_instant_le_p(res_til, aux.from)) {
		/* no danger then aye */
		goto out;
	} else if (!aux_blocks_p(this->mr, aux)) {
		/* not even blocked by aux */
		goto out;
	}

	/* invariant: AUX.FROM < RES_TIL */
	do {
		/* get next blocking event */
		echs_event_t nex = pop_aux_event(this);
		echs_idiff_t and;

		if (UNLIKELY(echs_nul_event_p(nex))) {
			/* state stream and event cache are finished,
			 * prepare to go to short-circuiting mode,
			 * RES will fit trivially between PREV_AUX and
			 * the most-distant event in the future */
			free_echs_evstrm(this->states);
			this->states = NULL;
			break;
		}

		/* check if RES would fit between AUX and NEX */
		and = echs_instant_diff(nex.from, aux_til);
		if (echs_idiff_le_p(res.dur, and)) {
			/* yep, would fit, forget about nex */
			push_aux_event(this, nex);
			break;
		} else if (aux_blocks_p(this->mr, nex)) {
			/* no fittee, `extend' aux */
			aux_til = echs_instant_add(nex.from, nex.dur);
		}
	} while (1);
	/* invariant AUX.TILL + (RES.TILL - RES.FROM) <= NEX.FROM */
	res.from = aux_til;
out:
	/* better save what we've got */
	push_aux_event(this, aux);
pop:
	if (popp) {
		(void)echs_evstrm_pop(this->movers);
	}
	return res;
}

static void
free_evmrul(echs_evstrm_t s)
{
	struct evmrul_s *this = (struct evmrul_s*)s;

	free_echs_evstrm(this->movers);
	if (LIKELY(this->states != NULL)) {
		free_echs_evstrm(this->states);
	}
	free(this);
	return;
}

static echs_evstrm_t
clone_evmrul(echs_const_evstrm_t s)
{
	const struct evmrul_s *this = (const struct evmrul_s*)s;
	struct evmrul_s *clon = malloc(sizeof(*this));

	*clon = *this;
	clon->movers = clone_echs_evstrm(this->movers);
	if (LIKELY(this->states != NULL)) {
		clon->states = clone_echs_evstrm(this->states);
	}
	return (echs_evstrm_t)clon;
}

static void
send_evmrul(int whither, echs_const_evstrm_t s)
{
	const struct evmrul_s *this = (const struct evmrul_s*)s;

	echs_evstrm_seria(whither, this->movers);
	mrulsp_icalify(whither, &this->mr);
	return;
}


/* public funs */
echs_evstrm_t
make_evmrul(const mrulsp_t *mr, echs_evstrm_t mov, echs_evstrm_t aux)
{
/* the AUX arg is usually not available at the time of calling,
 * we happily accept NULL for it and provide a means to hand it
 * in later. */
	struct evmrul_s *res;

	switch (mr->mdir) {
	case MDIR_PAST:
	case MDIR_PASTTHENFUTURE:
	case MDIR_FUTURE:
	case MDIR_FUTURETHENPAST:
		break;
	default:
		return NULL;
	}
	/* otherwise ... */
	res = malloc(sizeof(*res));
	switch (mr->mdir) {
	case MDIR_PAST:
	case MDIR_PASTTHENFUTURE:
		res->class = &evmrul_past_cls;
		break;
	case MDIR_FUTURE:
	case MDIR_FUTURETHENPAST:
		res->class = &evmrul_futu_cls;
		break;
	default:
		free(res);
		return NULL;
	}
	res->movers = mov;
	res->states = aux;
	res->mr = *mr;
	memset(res->aux, 0, sizeof(res->aux));
	return (echs_evstrm_t)res;
}

/* evmrul.c ends here */
