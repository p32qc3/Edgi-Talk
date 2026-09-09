#include <stdio.h>
#include <string.h>

#include "pet_service_status.h"

int main(void)
{
    char text[PET_SERVICE_STATUS_TEXT_MAX];

    pet_service_status_init();
    pet_service_status_set_version("AB1");
    if (!pet_service_status_format_current(text, sizeof(text)) ||
        strcmp(text, "AI:DEMO | 51:WAIT | AB1") != 0) {
        fprintf(stderr, "FAIL: initial AB1 status is not visible\n");
        return 1;
    }
    pet_service_status_set_ai_state(PET_AI_BUSY);
    pet_service_status_set_51_state(PET_51_LINK);
    if (!pet_service_status_format_current(text, sizeof(text)) ||
        strcmp(text, "AI:THINK | 51:LINK | AB1") != 0) {
        fprintf(stderr, "FAIL: current AI and 51 state is not formatted\n");
        return 1;
    }
    puts("PASS: service_status");
    return 0;
}
