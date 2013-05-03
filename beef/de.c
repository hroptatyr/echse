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
echs_stream_t easter;
echs_strdef_t g_east;


echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	echs_strdef_t east = echs_open_stream(i, "easter");
	echs_stream_t east_s = ECHS_SELECT(
		east.s, {
			"GOODFRI", "EASTER", "EASTERMON",
				"ASCENSION", "WHITSUN", "WHITMON"});
	echs_stream_t all[] = {
		newy = echs_every_year(i, JAN, 1),
		may1 = echs_every_year(i, MAY, 1),
		oct3 = echs_every_year(i, OCT, 3),
		xmas = echs_every_year(i, DEC, 25),
		boxd = echs_every_year(i, DEC, 26),
		easter = ECHS_RENAME(
			east_s, {
				{"GOODFRI", "Karfreitag"},
				{"EASTER", "Ostersonntag"},
				{"EASTERMON", "Ostermontag"},
				{"ASCENSION", "Christi_Himmelfahrt"},
				{"WHITSUN", "Pfingstsonntag"},
				{"WHITMON", "Pfingstmontag"}
			}),
	};

	g_east = east;
	echs_every_set_state(newy, "Neujahr");
	echs_every_set_state(may1, "Tag_der_Arbeit");
	echs_every_set_state(oct3, "Tag_der_Einheit");
	echs_every_set_state(xmas, "1._Weihnachtsfeiertag");
	echs_every_set_state(boxd, "2._Weihnachtsfeiertag");

	/* free temporaries */
	echs_free_select(east_s);
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
	echs_free_rename(easter);
	echs_close_stream(g_east);
	return;
}

/* de.c ends here */
