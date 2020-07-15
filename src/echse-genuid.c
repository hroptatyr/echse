/*** echse-genuid.c -- uid generator
 *
 * Copyright (C) 2013-2020 Sebastian Freundt
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "intern.h"
#include "echse-genuid.h"
#include "nifty.h"

static char *line;
static size_t llen;


static char
u2h(uint8_t c)
{
	switch (c) {
	case 0 ... 9:
		return (char)(c + '0');
	case 10 ... 15:
		return (char)(c + 'a' - 10);
	default:
		break;
	}
	return (char)'?';
}


void
echse_init_genuid(void)
{
	return;
}

void
echse_fini_genuid(void)
{
	if (LIKELY(line != NULL)) {
		free(line);
	}
	return;
}

int
echse_genuid1(const char *fmt, const char *fn, bool forcep)
{
	size_t fmtz = strlen(fmt);
	size_t fz = strlen(fn);
	char buf[4U/*UID:*/ + fmtz + fz + 8U/*sizeof(obint_t)*/ + 2U/*\n\0*/];
	bool seen_uid_p = true;
	char *bp = buf;
	char *hp = NULL;
	FILE *fp;

	if (UNLIKELY((fp = fopen(fn, "r")) == NULL)) {
		return -1;
	}

	/* prep the hash buffer */
	memcpy(bp, "UID:", 4U);
	bp += 4U;
	for (const char *fmp = fmt, *const ep = fmt + fmtz; fmp < ep; fmp++) {
		if (*fmp != '%') {
			*bp++ = *fmp;
		} else {
			switch (*++fmp) {
			case '%':
				*bp++ = '%';
				break;
			case 'f':
				with (char *xp = strrchr(fn, '/')) {
					if (xp++ != NULL) {
						memcpy(bp, xp, fz - (xp - fn));
						bp += fz - (xp - fn);
					} else {
						memcpy(bp, fn, fz);
						bp += fz;
					}
				}
				break;
			case 'x':
				/* hash in hex */
				if (LIKELY(hp == NULL)) {
					hp = bp;
					bp += 8U/*sizeof(obint_t)*/;
				}
				break;
			default:
				break;
			}
		}
	}
	/* finalise buf */
	*bp++ = '\n';
	*bp++ = '\0';

	for (ssize_t nrd; (nrd = getline(&line, &llen, fp)) > 0;) {
		if (!strncmp(line, "BEGIN:VEVENT", 12U)) {
			seen_uid_p = false;
		} else if (!strncmp(line, "UID:", 4U)) {
			if (!seen_uid_p && !forcep) {
				seen_uid_p = true;
			} else {
				continue;
			}
		} else if (!seen_uid_p && !strncmp(line, "SUMMARY:", 8U)) {
			obint_t ob;

			if (LIKELY(line[nrd - 1] == '\n')) {
				nrd--;
			}
			if (line[nrd - 1] == '\r') {
				nrd--;
			}
			/* run the uid-ifier */
			ob = obint(line + 8U, nrd - 8U);
			/* obint_name() should give us a proper line */
			for (char *restrict p = hp + 8U; p > hp; ob >>= 4U) {
				*--p = u2h((uint8_t)(ob & 0xfU));
			}
			/* and output the line */
			fputs(buf, stdout);
			seen_uid_p = true;
		}
		fputs(line, stdout);
	}

	fclose(fp);
	return 0;
}

echs_toid_t
echs_toid_gen(echs_task_t t)
{
	size_t len;

	if (UNLIKELY(t->cmd == NULL)) {
		return 0U;
	} else if (UNLIKELY((len = strlen(t->cmd)) == 0U)) {
		return 0U;
	}
	/* avoid interning because we don't want to waste space */
	return obint(t->cmd, len);
}

/* echse-genuid.c ends here */
