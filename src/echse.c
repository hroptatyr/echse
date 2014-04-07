/*** echse.c -- testing echse concept
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
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "echse.h"
#include "dt-strpf.h"
#include "nifty.h"


#if defined STANDALONE
#include "echse.yucc"

/* date range to scan through */
static echs_instant_t from;
static echs_instant_t till;

static int
cmd_merge(const struct yuck_cmd_merge_s argi[static 1U])
{
	echs_evstrm_t smux;
	unsigned int n = 0;

	with (echs_evstrm_t sarr[argi->nargs]) {
		size_t ns = 0UL;

		for (size_t i = 0UL; i < argi->nargs; i++) {
			const char *fn = argi->args[i];
			echs_evstrm_t s = make_echs_evstrm_from_file(fn);

			if (LIKELY(s != NULL)) {
				sarr[ns++] = s;
			}
		}
		/* now mux them all into one */
		smux = echs_evstrm_vmux(sarr, ns);
		/* kill the originals */
		for (size_t i = 0UL; i < ns; i++) {
			free_echs_evstrm(sarr[i]);
		}
	}
	if (UNLIKELY(smux == NULL)) {
		/* return early */
		return 1;
	}
	/* just get it out now */
	for (echs_event_t e;
	     !echs_event_0_p(e = echs_evstrm_next(smux)); n++) {
		char fbuf[32U];

		if (echs_instant_lt_p(e.from, from)) {
			continue;
		} else if (echs_instant_lt_p(till, e.from)) {
			break;
		}
		/* otherwise print */
		dt_strf(fbuf, sizeof(fbuf), e.from);
		printf("%s\t%s\n", fbuf, obint_name(e.uid));
	}
	free_echs_evstrm(smux);
	return 0;
}

int
main(int argc, char *argv[])
{
	/* command line options */
	yuck_t argi[1U];
	int rc = 0;

	if (yuck_parse(argi, argc, argv)) {
		rc = 1;
		goto out;
	}

	if (argi->from_arg) {
		from = dt_strp(argi->from_arg);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		from = (echs_instant_t){.y = 2000, .m = 1, .d = 1};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		from = echs_nul_instant();
		from.y = 2000;
		from.m = 1;
		from.d = 1;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	if (argi->till_arg) {
		till = dt_strp(argi->till_arg);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		till = (echs_instant_t){.y = 2037, .m = 12, .d = 31};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		till = echs_nul_instant();
		till.y = 2037;
		till.m = 12;
		till.d = 31;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	switch (argi->cmd) {
	default:
	case ECHSE_CMD_NONE:
		fputs("\
echse: no valid command given\n\
Try --help for a list of commands.\n", stderr);
		rc = 1;
		break;
	case ECHSE_CMD_MERGE:
		rc = cmd_merge((struct yuck_cmd_merge_s*)argi);
		break;
	}
	/* some global resources */
	clear_interns();
	clear_bufpool();
out:
	yuck_free(argi);
	return rc;
}
#endif	/* STANDALONE */

/* echse.c ends here */
