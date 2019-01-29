#ifndef  __MEM_UTIL_H__
#define  __MEM_UTIL_H__

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define    DBG_PRINT(fmt, args...) \
	do {\
		fprintf(stdout, "DBG: %s(%d)-%s:\n"fmt, __FILE__,__LINE__,__FUNCTION__,##args); \
		fflush(stdout); \
	} while (0)


#define    ERR_PRINT(fmt, args...) \
    do \
    { \
        fprintf(stderr, "error:"fmt, ##args); \
        fflush(stderr); \
    } while (0)

#define    SYS_ERR_PRINT(fmt, args...) \
    do \
    { \
        fprintf(stderr, "error: %s(%d)-%s:\n"fmt": %s", __FILE__,__LINE__,__FUNCTION__,##args, strerror(errno)); \
        fflush(stderr); \
    } while (0)


#define    NORMAL_PRINT(fmt, args...) \
	do {\
		        printf(fmt, ##args); \
		        fflush(stdout); \
	} while (0)

#define    VERBOSE_PRINT(fmt, args...) \
	do {\
		if (verbose_enabled()) { \
		        NORMAL_PRINT(fmt, ##args); \
		} \
	} while (0)

#endif

