#include <echse.h>
#include <strdef.h>
#include <builders.h>

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */

echs_stream_t newy;
echs_stream_t oct3;
echs_stream_t xmas;
echs_stream_t boxd;
echs_strdef_t east;


echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	echs_stream_t all[] = {
		newy = echs_every_year(i, JAN, 1),
		oct3 = echs_every_year(i, OCT, 3),
		xmas = echs_every_year(i, DEC, 25),
		boxd = echs_every_year(i, DEC, 26),
		(east = echs_open_stream(i, "easter")).s,
	};

	return echs_mux(countof(all), all);
}

void
free_echs_stream(echs_stream_t s)
{
	echs_free_mux(s);
	echs_free_every(newy);
	echs_free_every(oct3);
	echs_free_every(xmas);
	echs_free_every(boxd);
	echs_close_stream(east);
	return;
}

/* de.c ends here */
