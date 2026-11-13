/*
 * mru_config.c: read /cf/mru.cfg through OSAL's file interface.
 */
#include <stdio.h>
#include <string.h>

#include "osapi.h"
#include "mru_config.h"

#define MRU_CFG_MAX 2048

static void MRU_ParseLine(const char *line, MRU_Config_t *cfg, uint32 *bad)
{
    char               word[16] = {0};
    unsigned long      a        = 0;
    unsigned long long b        = 0;
    int                n;

    if (line[0] == '#' || line[0] == '\0')
    {
        return;
    }
    n = sscanf(line, "%15s %lu %llu", word, &a, &b);
    if (n == 2 && strcmp(word, "cycles") == 0)
    {
        cfg->cycles = (uint32)a;
    }
    else if (n == 2 && strcmp(word, "cycle_ms") == 0 && a > 0)
    {
        cfg->cycleMs = (uint32)a;
    }
    else if (n == 3 && a < 3 && cfg->nFaults < MRU_MAX_FAULTS &&
             (strcmp(word, "corrupt") == 0 || strcmp(word, "stuck") == 0 || strcmp(word, "hang") == 0))
    {
        MRU_Fault_t *f = &cfg->faults[cfg->nFaults++];
        f->kind        = (word[0] == 'c') ? MRU_FAULT_CORRUPT : ((word[0] == 's') ? MRU_FAULT_STUCK : MRU_FAULT_HANG);
        f->replica     = (uint8)a;
        f->cycle       = (uint64)b;
    }
    else
    {
        (*bad)++;
    }
}

uint32 MRU_ReadConfig(const char *path, MRU_Config_t *cfg)
{
    static char buf[MRU_CFG_MAX + 1];
    osal_id_t   fd = OS_OBJECT_ID_UNDEFINED;
    int32       got;
    uint32      bad = 0;
    char       *line;
    char       *next;

    memset(cfg, 0, sizeof(*cfg));
    cfg->cycles  = 400;
    cfg->cycleMs = 100;

    if (OS_OpenCreate(&fd, path, OS_FILE_FLAG_NONE, OS_READ_ONLY) != OS_SUCCESS)
    {
        return 0; /* no file: the defaults, no faults */
    }
    got = OS_read(fd, buf, MRU_CFG_MAX);
    OS_close(fd);
    if (got < 0)
    {
        return 1;
    }
    buf[got] = '\0';
    for (line = buf; line != NULL; line = next)
    {
        next = strchr(line, '\n');
        if (next != NULL)
        {
            *next = '\0';
            next++;
        }
        MRU_ParseLine(line, cfg, &bad);
    }
    return bad;
}
