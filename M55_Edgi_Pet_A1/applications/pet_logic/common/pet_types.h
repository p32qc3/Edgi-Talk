#ifndef PET_TYPES_H
#define PET_TYPES_H
#if defined(__C51__) || defined(__SDCC_mcs51)
typedef unsigned char u8;
typedef unsigned int u16;
typedef unsigned long u32;
#else
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#define PET_WINDOW_MS 86400000UL
#define PET_TRUST_MAX 10000U
#define PET_QUOTA_MAX 5000U /* 50.00 trust per powered-time window. */
#define PET_CATEGORIES 5
/* Durations use unsigned subtraction; service clocks at least once/second. */
enum PetEmotion {
    PET_NORMAL, PET_HAPPY, PET_ANGRY, PET_SAD,
    PET_DIZZY, PET_SLEEP, PET_THINKING, PET_EXCITED
};
enum PetInput {
    INPUT_HEAD, INPUT_HUG, INPUT_LIGHT, INPUT_HEAVY,
    INPUT_SPAM, INPUT_SLEEP, INPUT_WAKE
};
enum GameResult { GAME_WIN=1, GAME_FAIL, GAME_CANCEL, GAME_TIMEOUT };
#endif
