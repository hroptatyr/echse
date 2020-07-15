#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bituint63_t x = {0U};

	x = ass_bui63(x, 36U);
	printf("%lx\n", x);

	x = ass_bui63(x, 40U);
	printf("%lx\n", x);

	x = ass_bui63(x, 4U);
	printf("%lx\n", x);

	x = ass_bui63(x, 0U);
	printf("%lx\n", x);
	x = ass_bui63(x, 1U);
	printf("%lx\n", x);
	return 0;
}

/* bitint_test.c ends here */
