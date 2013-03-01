/*** echse-filter.c -- filtering events
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


static int
materialise(echs_event_t e)
{
	static char buf[256];
	char *bp = buf;

	/* BEST has the guy */
	bp += dt_strf(buf, sizeof(buf), e.when);
	*bp++ = '\t';

	{
		size_t e_whaz = strlen(e.what);
		memcpy(bp, e.what, e_whaz);
		bp += e_whaz;
	}
	*bp++ = '\n';
	*bp = '\0';
	if (write(STDOUT_FILENO, buf, bp - buf) < 0) {
		return -1;
	}
	return 0;
}


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
	/* stuff we ctor */
	echs_stream_t strm;
	echs_mod_t m;
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
/* INST, FILTER, STRDEF, NSTRDEF */
	typedef echs_stream_t(*make_stream_f)(echs_instant_t, ...);
	static struct echss_clo_s clo;
	va_list ap;
	echs_filter_t f;
	const char *const *in;
	size_t nin;
	echs_mod_f mf;

	va_start(ap, inst);
	f = va_arg(ap, echs_filter_t);
	in = va_arg(ap, const char *const*);
	nin = va_arg(ap, size_t);
	va_end(ap);

	if ((clo.m = echs_mod_open("echse")) == NULL) {
		printf("NOPE\n");
		goto out;
	} else if ((mf = echs_mod_sym(clo.m, "make_echs_stream")) == NULL) {
		printf("no m_e_s()\n");
		goto out;
	} else if ((clo.strm = ((make_stream_f)mf)(inst, in, nin)).f == NULL) {
		printf("no stream\n");
		goto out;
	}
	/* assign the filter */
	clo.filt = f;

	return (echs_stream_t){__stream, &clo};

out:
	return (echs_stream_t){NULL, &clo};
}

void
free_echs_stream(echs_stream_t s)
{
	typedef void(*free_stream_f)(echs_stream_t);
	struct echss_clo_s *clo = s.clo;
	echs_mod_f f;

	if (UNLIKELY(clo->m == NULL)) {
		return;
	} else if ((f = echs_mod_sym(clo->m, "free_echs_stream")) == NULL) {
		printf("no f_e_s()\n");
		goto clos_out;
	}
	((free_stream_f)f)(clo->strm);

clos_out:
	echs_mod_close(clo->m);
	return;
}


#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch"
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif /* __INTEL_COMPILER */
#include "echse-filter-clo.h"
#include "echse-filter-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#elif defined __GNUC__
# pragma GCC diagnostic warning "-Wswitch"
# pragma GCC diagnostic warning "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */

int
main(int argc, char *argv[])
{
	/* command line options */
	struct echs_args_info argi[1];
	echs_instant_t from;
	echs_instant_t till;
	echs_filter_t thif;
	echs_stream_t this;
	int res = 0;

	if (echs_parser(argc, argv, argi)) {
		res = 1;
		goto out;
	}

	if (argi->from_given) {
		from = dt_strp(argi->from_arg);
	} else {
		from = (echs_instant_t){2000, 1, 1};
	}

	if (argi->till_given) {
		till = dt_strp(argi->till_arg);
	} else {
		till = (echs_instant_t){2037, 12, 31};
	}

	if (argi->filter_given) {
		thif = make_echs_filter(from, argi->filter_arg);
	} else {
		thif = (echs_filter_t){__identity, NULL};
	}

	/* create a stream f(s) with f being THIF and s being S */
	this = make_echs_stream(from, thif, argi->inputs, argi->inputs_num);

	/* the iterator */
	for (echs_event_t e;
	     (e = echs_stream_next(this),
	      !__event_0_p(e) && __event_le_p(e, till));) {
		if (UNLIKELY(materialise(e) < 0)) {
			break;
		}
	}

	/* get all of them streams in here finished */
	free_echs_stream(this);

	/* free filter */
	free_echs_filter(thif);

out:
	echs_parser_free(argi);
	return res;
}

/* echse-filter.c ends here */
