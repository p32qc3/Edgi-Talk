#ifndef STORAGE_H
#define STORAGE_H
#include "pet_core.h"
#define SAVE_SIZE 51
/* Wire record only. The board must supply independent durable slots. */
int save_encode(const PetSaved *s, u32 generation, u8 out[SAVE_SIZE]);
int save_decode(const u8 in[SAVE_SIZE], PetSaved *s, u32 *generation);
/* 0/1 = chosen slot, -1 = both invalid. Inputs must have SAVE_SIZE bytes. */
int save_choose(const u8 *a, const u8 *b, PetSaved *s, u32 *generation);
#endif
