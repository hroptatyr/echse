#include <echse.h>
#include <strdef.h>
#include <builders.h>

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */

echs_stream_t newy;
echs_stream_t oct3;
echs_stream_t may1;
echs_stream_t xmas;
echs_stream_t boxd;
echs_stream_t easter_sel;
echs_stream_t ascension_sel;
echs_strdef_t east1;
echs_strdef_t east2;


echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	echs_stream_t all[] = {
		newy = echs_every_year(i, JAN, 1),
		may1 = echs_every_year(i, MAY, 1),
		oct3 = echs_every_year(i, OCT, 3),
		xmas = echs_every_year(i, DEC, 25),
		boxd = echs_every_year(i, DEC, 26),
		easter_sel = echs_select(
			(east1 = echs_open_stream(i, "easter")).s, "EASTER"),
		ascension_sel = echs_select(
			(east2 = echs_open_stream(i, "easter")).s, "ASCENSION"),
	};

	echs_every_set_state(newy, "Neujahr");
	echs_every_set_state(may1, "Tag_der_Arbeit");
	echs_every_set_state(oct3, "Tag_der_Einheit");
	echs_every_set_state(xmas, "1._Weihnachtsfeiertag");
	echs_every_set_state(boxd, "2._Weihnachtsfeiertag");
	return echs_mux(countof(all), all);
}

void
free_echs_stream(echs_stream_t s)
{
	echs_free_mux(s);
	echs_free_every(newy);
	echs_free_every(may1);
	echs_free_every(oct3);
	echs_free_every(xmas);
	echs_free_every(boxd);
	echs_free_select(easter_sel);
	echs_free_select(ascension_sel);
	echs_close_stream(east1);
	echs_close_stream(east2);
	return;
}

/* de.c ends here */
