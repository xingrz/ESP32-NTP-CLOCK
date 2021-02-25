#ifndef __REALEARTH_BLEC_PROC__
#define __REALEARTH_BLEC_PROC__

#include <stdint.h>

void blec_recv(void *buf, uint16_t len);
void blec_send(void *buf, uint16_t len);

#endif  // __REALEARTH_BLEC_PROC__
