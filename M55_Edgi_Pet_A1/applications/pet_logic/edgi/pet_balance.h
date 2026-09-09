#ifndef PET_BALANCE_H
#define PET_BALANCE_H

/* A1 tuning: trust uses hundredths (1 means 0.01 on screen). */
#define PET_INITIAL_MOOD 30U
#define PET_HEAD_MOOD_GAIN 3U
#define PET_HEAD_TRUST_GAIN 1U
#define PET_HUG_MOOD_GAIN 10U
#define PET_HUG_TRUST_GAIN 30U
#define PET_HUG_REACTION_MS 2000UL
#define PET_POSITIVE_COOLDOWN_MS 3000UL

/* Head/hug share a category and a powered-time reward window. */
#define PET_POSITIVE_FULL_REWARDS 30U
#define PET_POSITIVE_HALF_REWARDS 40U

#endif
