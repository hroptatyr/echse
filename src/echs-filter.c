/*** echs-filter.c -- filtering events
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
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"
#include "module.h"
#include "strdef.h"
#include "fltdef.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

#define logger(what, how, args...)	fprintf(stderr, how "\n", args)


/* myself as filter */
struct echsf_clo_s {
	echs_fltdef_t fd;
};

static echs_event_t
__identity(echs_event_t e, void *UNUSED(clo))
{
	return e;
}

static echs_event_t
__filter(echs_event_t e, void *clo)
{
	struct echsf_clo_s *x = clo;

	return echs_filter_next(x->fd.f, e);
}

echs_filter_t
make_echs_filter(echs_instant_t from, ...)
{
	static struct echsf_clo_s x;
	va_list ap;
	const char *fn;

	va_start(ap, from);
	fn = va_arg(ap, const char *);
	va_end(ap);

	if ((x.fd = echs_open_fltdef(from, fn)).m == NULL) {
		logger(LOG_ERR, "cannot use stream DSO %s", fn);
	}
	return (echs_filter_t){__filter, &x};
}

void
free_echs_filter(echs_filter_t f)
{
	struct echsf_clo_s *clo = f.clo;

	if (LIKELY(clo != NULL)) {
		echs_close_fltdef(clo->fd);
	}
	return;
}


/* myself as stream */
struct echss_clo_s {
	unsigned int drain;
	echs_filter_t filt;
	echs_stream_t strm;
};

static echs_event_t
__stream(void *clo)
{
	struct echss_clo_s *x = clo;

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
make_echs_stream(echs_instant_t inst, ...)
{
/* INST, FILTER, STREAM */
	typedef echs_stream_t(*make_stream_f)(echs_instant_t, ...);
	static struct echss_clo_s clo;
	va_list ap;

	va_start(ap, inst);
	clo.filt = va_arg(ap, echs_filter_t);
	clo.strm = va_arg(ap, echs_stream_t);
	va_end(ap);

	return (echs_stream_t){__stream, &clo};
}

void
free_echs_stream(echs_stream_t UNUSED(s))
{
	return;
}

/* echs-filter.c ends here */
