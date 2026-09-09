#ifndef STORAGE_JOURNAL_H
#define STORAGE_JOURNAL_H
#include "storage.h"

#define PET_JOURNAL_PAGE 256UL
#define PET_JOURNAL_RECORD 64U
enum { JOURNAL_EMPTY = 0, JOURNAL_LOADED = 1,
       JOURNAL_IO_ERROR = -1, JOURNAL_DAMAGED = -2, JOURNAL_BAD_ARGUMENT = -3 };
typedef struct {
    void *context;
    /* Offsets are relative to exactly two independent erase sectors. */
    int (*read)(void *, u32, u8 *, u32);
    int (*program)(void *, u32, const u8 *, u32);
    int (*erase)(void *, u32, u32);
} PetJournalIo;
typedef struct {
    PetJournalIo io;
    PetSaved saved;
    u32 sector_size, generation, next_page[2], damaged_records;
    int selected;
    u8 writable;
} PetJournal;

/* The caller supplies a real erase geometry; no automatic formatting of
 * nonempty storage without a valid record. All callbacks return 1 on success. */
int journal_open(PetJournal *j, const PetJournalIo *io, u32 sector_size);
int journal_commit(PetJournal *j, const PetSaved *saved);
#endif
