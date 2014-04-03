#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bituint31_t x = {0U};

	x = ass_bui31(x, 19U);
	printf("%x\n", x);

	x = ass_bui31(x, 4U);
	printf("%x\n", x);

	x = ass_bui31(x, 4U);
	printf("%x\n", x);

	x = ass_bui31(x, 0U);
	printf("%x\n", x);
	x = ass_bui31(x, 1U);
	printf("%x\n", x);
	return 0;
}

/* bitint_test.c ends here */
