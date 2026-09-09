#include "storage_journal.h"
#include <string.h>

static int empty(const u8 *bytes, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) if (bytes[i] != 0xffU) return 0;
    return 1;
}

int journal_open(PetJournal *j, const PetJournalIo *io, u32 sector_size)
{
    u8 bytes[PET_JOURNAL_RECORD];
    PetSaved saved;
    u32 page, generation, delta;
    unsigned slot;
    if (!j || !io || !io->read || !io->program || !io->erase ||
        sector_size < PET_JOURNAL_PAGE || sector_size > 0x100000UL ||
        sector_size % PET_JOURNAL_PAGE) return JOURNAL_BAD_ARGUMENT;
    memset(j, 0, sizeof(*j));
    j->io = *io; j->sector_size = sector_size; j->selected = -1;
    for (slot = 0; slot < 2; slot++) {
        for (page = 0; page < sector_size / PET_JOURNAL_PAGE; page++) {
            if (!io->read(io->context, slot * sector_size + page * PET_JOURNAL_PAGE,
                          bytes, PET_JOURNAL_RECORD)) return JOURNAL_IO_ERROR;
            if (empty(bytes, PET_JOURNAL_RECORD)) continue;
            j->next_page[slot] = page + 1;
            if (!save_decode(bytes, &saved, &generation)) {
                j->damaged_records++; continue;
            }
            delta = (u32)(generation - j->generation);
            if (j->selected < 0 || (delta && delta < 0x80000000UL)) {
                j->saved = saved; j->generation = generation;
                j->selected = (int)slot;
            }
        }
    }
    if (j->selected < 0 && j->damaged_records) return JOURNAL_DAMAGED;
    j->writable = 1;
    return j->selected < 0 ? JOURNAL_EMPTY : JOURNAL_LOADED;
}

int journal_commit(PetJournal *j, const PetSaved *saved)
{
    u8 bytes[PET_JOURNAL_RECORD], verify[PET_JOURNAL_RECORD];
    PetSaved decoded;
    u32 generation, verified_generation, address, pages;
    unsigned slot;
    if (!j || !saved || !j->writable) return JOURNAL_BAD_ARGUMENT;
    generation = j->generation + 1UL;
    memset(bytes, 0xff, sizeof(bytes));
    if (!save_encode(saved, generation, bytes)) return JOURNAL_BAD_ARGUMENT;
    pages = j->sector_size / PET_JOURNAL_PAGE;
    slot = j->selected < 0 ? 0U : (unsigned)j->selected;
    if (j->next_page[slot] >= pages) slot ^= 1U;
    if (j->next_page[slot] >= pages) {
        /* The current valid record must survive erasure of the other sector. */
        if ((int)slot == j->selected) return JOURNAL_BAD_ARGUMENT;
        if (!j->io.erase(j->io.context, slot * j->sector_size, j->sector_size))
            return JOURNAL_IO_ERROR;
        j->next_page[slot] = 0;
    }
    address = slot * j->sector_size + j->next_page[slot] * PET_JOURNAL_PAGE;
    /* A failed/partial write consumes its page. Never reprogram an ECC unit. */
    j->next_page[slot]++;
    if (!j->io.program(j->io.context, address, bytes, PET_JOURNAL_RECORD) ||
        !j->io.read(j->io.context, address, verify, PET_JOURNAL_RECORD) ||
        memcmp(bytes, verify, sizeof(bytes)) ||
        !save_decode(verify, &decoded, &verified_generation) ||
        verified_generation != generation) return JOURNAL_IO_ERROR;
    j->saved = decoded; j->generation = generation; j->selected = (int)slot;
    return JOURNAL_LOADED;
}
