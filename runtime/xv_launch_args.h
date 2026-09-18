#pragma once
#include <string.h>
/* Vita LoadExec may put the first supplied argument at argv[0]. Accept that
 * and a conventional executable-name prefix; never use argument paths. */
static inline int xv_launch_has(int argc,char **argv,const char *flag)
{
    for(int i=0;i<argc;i++)if(argv[i]&&!strcmp(argv[i],flag))return 1;
    return 0;
}
static inline int xv_launch_slot(int argc,char **argv)
{
    if(xv_launch_has(argc,argv,"--xita-slot=0"))return 0;
    if(xv_launch_has(argc,argv,"--xita-slot=1"))return 1;
    return -1;
}
