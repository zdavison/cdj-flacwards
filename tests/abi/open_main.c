/* See open_call.S. Exit code 0 = pass. */
#include <stdint.h>
#include "../../src/vfs_hook.h"

uint32_t fw_call_open(uint32_t a, const uint32_t id[2], void *fn);
uint32_t fake_open(void);
uint32_t fake_rec[3];

int main(void)
{
    static const uint32_t id[2] = { 0x11112222u, 0x33334444u };
    static vh_stock_t fake;

    fake.open = (void *)fake_open;
    vh_stock = &fake;
    if (fw_call_open(0xAAAA5555u, id, (void *)hook_open) != 0)
        return 4;
    if (fake_rec[0] != 0xAAAA5555u)
        return 1;
    if (fake_rec[1] != id[0])
        return 2;
    if (fake_rec[2] != id[1])
        return 3;
    return 0;
}
