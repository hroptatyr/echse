#include <echse.h>
#include <strdef.h>
#include <builders.h>

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */

echs_stream_t newy;
echs_stream_t mayd;
echs_stream_t spring;
echs_stream_t summer;
echs_stream_t easter;
echs_stream_t xbox;
echs_stream_t xmas;
echs_stream_t boxd;
echs_stream_t rwed;
echs_strdef_t g_east;


echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	echs_strdef_t east = echs_open_stream(i, "easter");
	echs_stream_t sat;
	echs_stream_t sun;
	echs_stream_t we;

	newy = echs_every_year(i, JAN, 1U);
	mayd = echs_every_year(i, MAY, FIRST(MON));
	spring = echs_every_year(i, MAY, LAST(MON));
	summer = echs_every_year(i, AUG, LAST(MON));
	easter = ECHS_SELECT(east.s, {"GOODFRI", "EASTER", "EASTERMON"});

	xmas = echs_every_year(i, DEC, 25);
	boxd = echs_every_year(i, DEC, 26);
	echs_every_set_state(xmas, "Christmas");
	echs_every_set_state(boxd, "Boxing_Day");
	/* blocker stream */
	sat = echs_every_week(i, SAT);
	sun = echs_every_week(i, SUN);
	we = ECHS_MUX(sat, sun);
	xbox = echs_move_after(we, ECHS_MUX(xmas, boxd));

	echs_event_t w = {
		{2011, 4, 29, ECHS_ALL_DAY},
		"Wedding_of_Prince_William_and_Catherine_Middleton"
	};
	rwed = ECHS_STREAM(i, w);

	g_east = east;
	echs_every_set_state(mayd, "Mayday");
	echs_every_set_state(newy, "New_Year");
	echs_every_set_state(summer, "Summer_Bank_Holiday");
	echs_every_set_state(spring, "Spring_Bank_Holiday");

	return ECHS_MUX(newy, mayd, spring, summer, easter, xbox, rwed);
}

void
free_echs_stream(echs_stream_t s)
{
	echs_free_mux(s);
	echs_free_every(newy);
	echs_free_every(mayd);
	echs_free_every(summer);
	echs_free_every(spring);
	echs_free_select(easter);
	echs_free_move(xbox);
	echs_free_stream(rwed);

	/* free tentative resources */
	echs_free_every(xmas);
	echs_free_every(boxd);

	echs_close_stream(g_east);
	return;
}

/* uk.c ends here */
