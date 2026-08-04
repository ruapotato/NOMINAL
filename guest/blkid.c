/* /sbin/blkid — what uuid does this disk actually carry?
 *
 * Several faults hinge on a uuid that is well-formed and simply not this
 * disk's: zbl.cfg names one, /etc/fstab names another, and the initrd waits
 * for a device that will never appear. Without this there is no way to check
 * either of them against reality, only against each other -- and two configs
 * agreeing with each other and not with the disk is exactly the fault.
 */
#include "gsys.h"
static char uuid[64];
void _start(void)
{
    if (g_rootuuid(uuid, sizeof uuid) <= 0) {
        g_putln("blkid: cannot read the disk");
        g_exit(1);
    }
    g_puts("/dev/sda1: UUID=\"");
    g_puts(uuid);
    g_putln("\" TYPE=\"ext4\"");
    g_putln("/dev/sr0:  TYPE=\"iso9660\"");
    g_exit(0);
}
