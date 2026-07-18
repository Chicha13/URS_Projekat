#ifndef ALS_PROCESS_H
#define ALS_PROCESS_H
#include <stdint.h>

void  als_update_cache(void);
int   compute_lux(uint16_t rawr, uint16_t rawg, uint16_t rawb, uint16_t rawc, float *lux);
int   compute_cct(uint16_t rawb, uint16_t rawr, float *cct);
int   compute_hue_saturation(uint16_t rawr, uint16_t rawg, uint16_t rawb, float *hue, float *saturation);
int   cct_to_xy(float CCT, float *cx, float *cy);
void  process_als_cycle(uint16_t rawr, uint16_t rawg, uint16_t rawb, uint16_t rawc);


#endif
