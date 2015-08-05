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
#include <errno.h>
#include <time.h>

#include "echse.h"
#include "echse-genuid.h"
#include "evical.h"
#include "dt-strpf.h"
#include "nifty.h"

struct unroll_param_s {
	echs_instant_t from;
	echs_instant_t till;
	struct rrulsp_s filt;
};


static __attribute__((format(printf, 1, 2))) void
serror(const char *fmt, ...)
{
	va_list vap;
	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
	if (errno) {
		fputc(':', stderr);
		fputc(' ', stderr);
		fputs(strerror(errno), stderr);
	}
	fputc('\n', stderr);
	return;
}

static size_t
xstrlncpy(char *restrict dst, size_t dsz, const char *src, size_t ssz)
{
	if (ssz > dsz) {
		ssz = dsz - 1U;
	}
	memcpy(dst, src, ssz);
	dst[ssz] = '\0';
	return ssz;
}

static size_t
xstrlcpy(char *restrict dst, const char *src, size_t dsz)
{
	return xstrlncpy(dst, dsz, src, strlen(src));
}

static void
chck_filt(struct rrulsp_s *restrict f)
{
	if (bi31_has_bits_p(f->dom) ||
	    bi447_has_bits_p(&f->dow) ||
	    bui31_has_bits_p(f->mon) ||
	    bi63_has_bits_p(f->wk) ||
	    bi383_has_bits_p(&f->doy)) {
		/* indicate active filter */
		f->freq = FREQ_YEARLY;
	}
	return;
}

static void
unroll_ical(echs_evstrm_t smux, struct unroll_param_s p)
{
	echs_event_t e;

	/* just get it out now */
	echs_prnt_ical_init();
	while (!echs_event_0_p(e = echs_evstrm_next(smux))) {
		if (echs_instant_lt_p(e.from, p.from)) {
			continue;
		} else if (echs_instant_lt_p(p.till, e.from)) {
			break;
		}
		/* otherwise print */
		echs_prnt_ical_event(e);
	}
	echs_prnt_ical_fini();
	return;
}

static char*
unroll_prnt(char *restrict buf, size_t bsz, echs_event_t e, const char *fmt)
{
	char *restrict bp = buf;
	const char *const ebp = buf + bsz;

	for (const char *fp = fmt; *fp && bp < ebp; fp++) {
		if (UNLIKELY(*fp == '\\')) {
			static const char esc[] =
				"\a\bcd\e\fghijklm\nopq\rs\tu\v";

			if (*++fp && *fp >= 'a' && *fp <= 'v') {
				*bp++ = esc[*fp - 'a'];
			} else if (!*fp) {
				/* huh? trailing lone backslash */
				break;
			} else {
				*bp++ = *fp;
			}
		} else if (UNLIKELY(*fp == '%')) {
			echs_instant_t i;
			obint_t x;

			switch (*++fp) {
			case 'b':
				i = e.from;
				goto cpy_inst;
			case 'e':
				i = e.till;
				goto cpy_inst;
			case 's':
				continue;
			case 'u':
				if ((x = e.oid)) {
					goto cpy_obint;
				}
				continue;
			case '%':
				*bp++ = '%';
			default:
				continue;
			}

		cpy_inst:
			bp += dt_strf(bp, ebp - bp, i);
			continue;
		cpy_obint:
			{
				const char *nm = obint_name(x);
				const size_t nz = obint_len(x);

				bp += xstrlncpy(bp, ebp - bp, nm, nz);
			}
			continue;
		} else {
			*bp++ = *fp;
		}
	}
	return bp;
}

static void
unroll_frmt(echs_evstrm_t smux, struct unroll_param_s p, const char *fmt)
{
	const size_t fmz = strlen(fmt);
	char fbuf[fmz + 2 * 32U + 2 * 256U];
	echs_event_t e;

	/* just get it out now */
	while (!echs_event_0_p(e = echs_evstrm_next(smux))) {
		char *restrict bp;

		if (echs_instant_lt_p(p.till, e.from)) {
			break;
		} else if (echs_instant_lt_p(e.from, p.from)) {
			continue;
		} else if (p.filt.freq &&
			   !echs_instant_matches_p(&p.filt, e.from)) {
			continue;
		}
		/* otherwise print */
		bp = unroll_prnt(fbuf, sizeof(fbuf) - 2U, e, fmt);
		/* finalise buf */
		*bp++ = '\n';
		*bp++ = '\0';

		fputs(fbuf, stdout);
	}
	return;
}


#if defined STANDALONE
#include "echse.yucc"

static int
cmd_unroll(const struct yuck_cmd_unroll_s argi[static 1U])
{
	static const char dflt_fmt[] = "%b\t%s";
	/* params that filter the output */
	struct unroll_param_s p = {.filt = {.freq = FREQ_NONE}};
	echs_evstrm_t smux;

	if (argi->from_arg) {
		p.from = dt_strp(argi->from_arg);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		p.from = (echs_instant_t){.y = 2000, .m = 1, .d = 1};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		p.from = echs_nul_instant();
		p.from.y = 2000;
		p.from.m = 1;
		p.from.d = 1;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	if (argi->till_arg) {
		p.till = dt_strp(argi->till_arg);
	} else {
#if defined HAVE_ANON_STRUCTS_INIT
		p.till = (echs_instant_t){.y = 2037, .m = 12, .d = 31};
#else  /* !HAVE_ANON_STRUCTS_INIT */
		p.till = echs_nul_instant();
		p.till.y = 2037;
		p.till.m = 12;
		p.till.d = 31;
#endif	/* HAVE_ANON_STRUCTS_INIT */
	}

	if (argi->filter_arg) {
		const char *sel = argi->filter_arg;
		const size_t len = strlen(sel);

		/* just use the rrule parts, there won't be a frequency set */
		p.filt = echs_read_rrul(sel, len);
		/* quickly assess the rrule, set a frequency if there's
		 * stuff to be filtered */
		chck_filt(&p.filt);
	}

	with (echs_evstrm_t sarr[argi->nargs]) {
		size_t ns = 0UL;

		for (size_t i = 0UL; i < argi->nargs; i++) {
			const char *fn = argi->args[i];
			echs_evstrm_t s = make_echs_evstrm_from_file(fn);

			if (UNLIKELY(s == NULL)) {
				serror("\
echse: Error: cannot open file `%s'", fn);
				continue;
			}
			sarr[ns++] = s;
		}
		/* now mux them all into one */
		smux = echs_evstrm_vmux(sarr, ns);
		/* kill the originals */
		for (size_t i = 0UL; i < ns; i++) {
			free_echs_evstrm(sarr[i]);
		}
	}
	if (argi->nargs == 0UL) {
		/* read from stdin */
		smux = make_echs_evstrm_from_file(NULL);
	}
	if (UNLIKELY(smux == NULL)) {
		/* return early */
		return 1;
	}
	if (argi->format_arg != NULL && !strcmp(argi->format_arg, "ical")) {
		/* special output format */
		unroll_ical(smux, p);
	} else {
		const char *fmt = argi->format_arg ?: dflt_fmt;
		unroll_frmt(smux, p, fmt);
	}

	free_echs_evstrm(smux);
	return 0;
}

static int
cmd_genuid(const struct yuck_cmd_genuid_s argi[static 1U])
{
	const bool forcep = argi->force_flag;
	const char *fmt = argi->format_arg ?: "echse/%f/%x@example.com";
	int rc = 0;

	echse_init_genuid();
	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];

		if (echse_genuid1(fmt, fn, forcep) < 0) {
			rc++;
		}
	}
	echse_fini_genuid();
	return rc;
}

static int
cmd_merge(const struct yuck_cmd_merge_s argi[static 1U])
{
	echs_prnt_ical_init();
	for (size_t i = 0UL; i < argi->nargs; i++) {
		const char *fn = argi->args[i];
		echs_evstrm_t s = make_echs_evstrm_from_file(fn);

		if (UNLIKELY(s == NULL)) {
			serror("\
echse: Error: cannot open file `%s'", fn);
			continue;
		}
		/* print the guy */
		echs_evstrm_prnt(s);

		/* and free him */
		free_echs_evstrm(s);
	}
	echs_prnt_ical_fini();
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

	switch (argi->cmd) {
	default:
	case ECHSE_CMD_NONE:
		fputs("\
echse: no valid command given\n\
Try --help for a list of commands.\n", stderr);
		rc = 1;
		break;
	case ECHSE_CMD_UNROLL:
		rc = cmd_unroll((struct yuck_cmd_unroll_s*)argi);
		break;
	case ECHSE_CMD_GENUID:
		rc = cmd_genuid((struct yuck_cmd_genuid_s*)argi);
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
