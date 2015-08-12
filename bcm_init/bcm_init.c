#include "bcm_host.h"
__attribute__((constructor)) static void init_bcm_omx (void)
{
   bcm_host_init();
}


__attribute__((destructor)) static void deinit_bcm_omx(void)
{
   bcm_host_deinit();
}