/*** task.c -- gathering task properties
 *
 * Copyright (C) 2013-2015 Sebastian Freundt
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
#include <string.h>
#include "task.h"
#include "evfilt.h"
#include "nifty.h"
#include "nummapstr.h"

struct echs_task_s*
echs_task_clone(echs_task_t t)
{
	struct echs_task_s *res = malloc(sizeof(*res));
	const char *tmps;

	*res = *t;
	if (t->cmd != NULL) {
		res->cmd = strdup(t->cmd);
	}
	if ((tmps = nummapstr_str(t->owner)) != NULL) {
		res->owner = nummapstr_bang_str(strdup(tmps));
	}
	if (t->run_as.wd != NULL) {
		res->run_as.wd = strdup(t->run_as.wd);
	}
	if (t->run_as.sh != NULL) {
		res->run_as.sh = strdup(t->run_as.sh);
	}
	if (t->env != NULL) {
		res->env = clone_strlst(t->env);
	}
	if (t->org != NULL) {
		res->org = strdup(t->org);
	}
	if (t->att != NULL) {
		res->att = clone_strlst(t->att);
	}
	return res;
}

void
free_echs_task(echs_task_t t)
{
	struct echs_task_s *restrict tmpt = deconst(t);
	char *tmps;

	if (tmpt->strm) {
		free_echs_evstrm(tmpt->strm);
	}
	if (tmpt->cmd) {
		free(deconst(tmpt->cmd));
	}
	if ((tmps = nummapstr_str(t->owner))) {
		free(tmps);
	}
	if (tmpt->run_as.wd) {
		free(deconst(tmpt->run_as.wd));
	}
	if (tmpt->run_as.sh) {
		free(deconst(tmpt->run_as.sh));
	}
	if (tmpt->env) {
		free_strlst(tmpt->env);
	}
	if (tmpt->org) {
		free(deconst(tmpt->org));
	}
	if (tmpt->att) {
		free_strlst(tmpt->att);
	}
	free(tmpt);
	return;
}

int
echs_task_rset_toid(echs_task_t t, echs_toid_t oid)
{
	struct echs_task_s *restrict tmpt = deconst(t);

	tmpt->oid = oid;
	return 0;
}

int
echs_task_rset_ownr(echs_task_t t, unsigned int uid)
{
	struct echs_task_s *restrict tmpt = deconst(t);
	char *tmps;

	if (UNLIKELY((tmps = nummapstr_str(t->owner)) != NULL)) {
		free(tmps);
	}
	tmpt->owner = nummapstr_bang_num(uid);
	return 0;
}

/* task.c ends here */
