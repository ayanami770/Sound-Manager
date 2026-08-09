/*
 * CRC-32 (IEEE 802.3, polynomial 0xEDB88320) as used by the zip format.
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#include <stddef.h>

static unsigned int crc_table[256];
static int crc_table_ready = 0;

static void crc32_init(void)
{
    for (unsigned int n = 0; n < 256; n++)
    {
        unsigned int c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        crc_table[n] = c;
    }
    crc_table_ready = 1;
}

/* Start with crc = 0, feed data in any number of chunks. */
unsigned int smn_crc32(unsigned int crc, const unsigned char *buf, size_t len)
{
    if (!crc_table_ready)
        crc32_init();
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}
