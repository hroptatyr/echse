/*** logger.h -- logging service
 *
 * Copyright (C) 2011-2012 Sebastian Freundt
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
#if !defined INCLUDED_logger_h_
#define INCLUDED_logger_h_

#include <syslog.h>

#define ECHS_LOG_FLAGS		(LOG_PID | LOG_NDELAY)
#define ECHS_FACILITY		(LOG_LOCAL4)
#define ECHS_MAKEPRI(x)		(x)
#define ECHS_SYSLOG(x, args...)	echs_log(ECHS_MAKEPRI(x), args)


extern void(*echs_log)(int prio, const char *fmt, ...);

extern __attribute__((format(printf, 2, 3))) void
echs_errlog(int prio, const char *fmt, ...);

/* convenience macros */
#define ECHS_DEBUG(args...)
#define ECHS_DBGCONT(args...)

#if defined ECHS_LOG_PREFIX
# define ECHS_LOG_XPRE		ECHS_LOG_PREFIX " "
#else  /* !ECHS_LOG_PREFIX */
# define ECHS_LOG_XPRE
#endif	/* ECHS_LOG_PREFIX */

#define ECHS_INFO_LOG(args...)					\
	do {							\
		ECHS_SYSLOG(LOG_INFO, ECHS_LOG_XPRE args);	\
		ECHS_DEBUG("INFO " args);			\
	} while (0)
#define ECHS_ERR_LOG(args...)						\
	do {								\
		ECHS_SYSLOG(LOG_ERR, ECHS_LOG_XPRE "ERROR " args);	\
		ECHS_DEBUG("ERROR " args);				\
	} while (0)
#define ECHS_CRIT_LOG(args...)						\
	do {								\
		ECHS_SYSLOG(LOG_CRIT, ECHS_LOG_XPRE "CRITICAL " args);	\
		ECHS_DEBUG("CRITICAL " args);				\
	} while (0)
#define ECHS_NOTI_LOG(args...)						\
	do {								\
		ECHS_SYSLOG(LOG_NOTICE, ECHS_LOG_XPRE "NOTICE " args);	\
		ECHS_DEBUG("NOTICE " args);				\
	} while (0)


static inline void
echs_openlog(void)
{
	openlog("echsd", ECHS_LOG_FLAGS, ECHS_FACILITY);
	return;
}

static inline void
echs_closelog(void)
{
	closelog();
	return;
}

#endif	/* INCLUDED_logger_h_ */
