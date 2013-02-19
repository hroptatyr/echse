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
#include <stdio.h>
#include <string.h>
#include "echse.h"
#include "instant.h"
#include "dt-strpf.h"

#if defined __INTEL_COMPILER
# pragma warning (disable:1419)
#endif	/* __INTEL_COMPILER */

extern void *init_file(const char*);

struct clo_s {
	FILE *f;
	char *line;
	size_t llen;
};


void*
init_file(const char *fn)
{
	static struct clo_s clo[1];

	if ((clo->f = fopen(fn, "r")) == NULL) {
		return ECHS_FAILED;
	}
	return clo;
}

echs_event_t
echs_stream(echs_instant_t inst, void *clo)
{
	struct clo_s *x = clo;
	ssize_t nrd;

	while ((nrd = getline(&x->line, &x->llen, x->f)) > 0) {
		/* read the line */
		const char *p;
		char *eol;
		echs_instant_t i;

		if ((p = strchr(x->line, '\t')) == NULL) {
			continue;
		} else if ((eol = strchr(x->line, '\n')) == NULL) {
			continue;
		} else if (__inst_0_p(i = dt_strp(x->line))) {
			continue;
		} else if (__inst_lt_p(i, inst)) {
			continue;
		}
		/* otherwise it's a match */
		*eol = '\0';
		return (echs_event_t){.when = i, .what = p + 1};
	}
	return (echs_event_t){0};
}

int
fini_stream(void *clo)
{
	struct clo_s *x = clo;

	if (x->f != NULL) {
		fclose(x->f);
	}
	if (x->line != NULL) {
		free(x->line);
	}
	x->f = NULL;
	x->line = NULL;
	x->llen = 0UL;
	return 0;
}

/* echs-file.c ends here */
