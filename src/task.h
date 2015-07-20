/*** task.h -- gathering task properties
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
#if !defined INCLUDED_task_h_
#define INCLUDED_task_h_

#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef const struct echs_task_s *echs_task_t;
typedef uintptr_t echs_evuid_t;

typedef struct {
	uid_t u;
	gid_t g;
} cred_t;

struct echs_task_s {
	echs_evuid_t uid;

	/* command, environment, working dir */
	echs_evuid_t cmd;
	echs_evuid_t *env;
	echs_evuid_t cwd;

	/* credentials we want this job run as */
	cred_t run_as;

	/* the organiser and attendees of the whole shebang */
	const char *org;
	const char **att;
};


/* convenience */
static inline __attribute__((const, pure)) bool
echs_task_eq_p(echs_task_t t1, echs_task_t t2)
{
	return t1 == t2 || t1->uid == t2->uid;
}

#endif	/* INCLUDED_task_h_ */
