/*** echs-file.c -- echs file stream
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
#include <stdarg.h>
#include <stdio.h>
#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)	__attribute__((unused)) x
#endif	/* UNUSED */

struct clo_s {
	FILE *f;
	char *line;
	size_t llen;
};

static echs_event_t
echs_file_stream(void *clo)
{
	struct clo_s *x = clo;
	ssize_t nrd;
	const char *p;
	echs_instant_t i;

	if ((nrd = getline(&x->line, &x->llen, x->f)) <= 0) {
		goto nul;
	} else if ((p = strchr(x->line, '\t')) == NULL) {
		goto nul;
	} else if (__inst_0_p(i = dt_strp(x->line))) {
		goto nul;
	}
	/* otherwise it's a match */
	x->line[--nrd] = '\0';
	return (echs_event_t){.when = i, .what = p + 1};
nul:
	return (echs_event_t){0};
}


echs_stream_t
make_echs_stream(echs_instant_t inst, ...)
{
/* wants a const char *fn */
	va_list ap;
	const char *fn;
	FILE *f;
	struct clo_s *clo;
	ssize_t nrd;

	va_start(ap, inst);
	fn = va_arg(ap, const char*);
	va_end(ap);

	if ((f = fopen(fn, "r")) == NULL) {
		return (echs_stream_t){NULL};
	}
	/* otherwise set up the closure */
	clo = calloc(1, sizeof(*clo));
	clo->f = f;

	while ((nrd = getline(&clo->line, &clo->llen, f)) > 0) {
		echs_instant_t i;

		if (__inst_0_p(i = dt_strp(clo->line))) {
			goto nul;
		} else if (__inst_lt_p(inst, i)) {
			/* ungetline() */
			fseek(f, -nrd, SEEK_CUR);
			break;
		}
	}
	return (echs_stream_t){echs_file_stream, clo};
nul:
	return (echs_stream_t){NULL, clo};
}

void
free_echs_stream(echs_stream_t x)
{
	struct clo_s *clo = x.clo;

	if (UNLIKELY(clo == NULL)) {
		/* oh god, how did we get here? */
		return;
	}
	if (clo->f != NULL) {
		fclose(clo->f);
	}
	if (clo->line != NULL) {
		free(clo->line);
	}
	clo->f = NULL;
	clo->line = NULL;
	clo->llen = 0UL;
	free(clo);
	return;
}

/* echs-file.c ends here */
