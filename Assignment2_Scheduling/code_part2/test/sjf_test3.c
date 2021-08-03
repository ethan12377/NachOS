#include "syscall.h"
main()
	{
		int	i;
		for (i=0;i<5;i++) {
			PrintInt(300+i*10);
            Sleep(1);
        }
	}
