/*
 * Re-export FAL config so rt-thread/components/fal can #include <fal_cfg.h>
 * when only the board directory is on the global include path (same pattern as WIFI BSP).
 */
#include "../libraries/Common/board/ports/fal/fal_cfg.h"
