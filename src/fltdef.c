/*** fltdef.c -- filter modules, files, sockets, etc.
 *
 * Copyright (C) 2013 Sebastian Freundt
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
#include <sys/stat.h>

#include "echse.h"
#include "fltdef.h"
#include "module.h"
#include "instant.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */


echs_fltdef_t
echs_open_fltdef(echs_instant_t i, const char *fltdef)
{
	typedef echs_filter_t(*make_filter_f)(echs_instant_t, ...);
	struct echs_fltdef_s res;

	/* try the module thing first */
	if ((res.m = echs_mod_open(fltdef)) != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "make_echs_filter")) == NULL) {
			/* nope, unsuitable */
			echs_mod_close(res.m);
			goto fuckup;
		} else if ((res.f = ((make_filter_f)f)(i)).f == NULL) {
			/* no filters returned */
			echs_close_fltdef(res);
			goto fuckup;
		}
	} else {
	fuckup:
		/* super fuckup */
		return (echs_fltdef_t){.m = NULL};
	}
	return res;
}

void
echs_close_fltdef(echs_fltdef_t sd)
{
	typedef void(*free_filter_f)(echs_filter_t);

	if (sd.m != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(sd.m, "free_echs_filter")) != NULL) {
			/* call the finaliser */
			((free_filter_f)f)(sd.f);
		}

		/* and (or otherwise) close the module */
		echs_mod_close(sd.m);
	}
	return;
}

void(*
	    echs_fltdef_psetter(echs_fltdef_t sf)
	)(echs_filter_t, const char*, struct filter_pset_s)
{
	typedef void(*filter_pset_f)(
		echs_filter_t, const char*, struct filter_pset_s);

	if (sf.m != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(sf.m, "echs_filter_pset")) != NULL) {
			return (filter_pset_f)f;
		}
	}
	return NULL;
}

void
echs_fltdef_pset(echs_fltdef_t sf, const char *k, struct filter_pset_s v)
{
	void(*f)(echs_filter_t, const char*, struct filter_pset_s);

	if ((f = echs_fltdef_psetter(sf)) != NULL) {
		f(sf.f, k, v);
	}
	return;
}


/* service to turn a filter plus a stream into a stream */
struct fltstr_clo_s {
	unsigned int drain;
	echs_filter_t filt;
	echs_stream_t strm;
};

static echs_event_t
__stream(void *clo)
{
	struct fltstr_clo_s *x = clo;

	switch (x->drain) {
	case 0:
		/* iterate! */
		for (echs_event_t e;
		     (e = echs_stream_next(x->strm), !__event_0_p(e));) {
			if (!__event_0_p(e = echs_filter_next(x->filt, e))) {
				return e;
			}
		}
		x->drain = 1;
	case 1:
		/* drain */
		for (echs_event_t e;
		     (e = echs_filter_drain(x->filt), !__event_0_p(e));) {
			return e;
		}
	default:
		break;
	}
	return (echs_event_t){0};
}

echs_stream_t
make_echs_filtstrm(echs_filter_t f, echs_stream_t s)
{
	struct fltstr_clo_s *clo;

	clo = calloc(1, sizeof(*clo));
	clo->filt = f;
	clo->strm = s;

	return (echs_stream_t){__stream, clo};
}

void
free_echs_filtstrm(echs_stream_t s)
{
	struct fltstr_clo_s *clo = s.clo;

	if (LIKELY(clo != NULL)) {
		clo->filt = (echs_filter_t){NULL};
		clo->strm = (echs_stream_t){NULL};
		free(clo);
	}
	return;
}

/* fltdef.c ends here */
