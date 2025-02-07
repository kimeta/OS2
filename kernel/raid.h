//
// Created by mihailo on 2/4/25.
//

#ifndef XV6_RISCV_OS2_RSICV_RAID_RAID_H
#define XV6_RISCV_OS2_RSICV_RAID_RAID_H
enum RAID_TYPE{RAID0, RAID1, RAID0_1, RAID4, RAID5};

int sys_init_raid_impl(enum RAID_TYPE raid);
int sys_read_raid_impl(int blkn, uchar* data);
int sys_write_raid_impl(int blkn, uchar* data);
int sys_disk_fail_raid_impl(int diskn);
int sys_disk_repaired_raid_impl(int diskn);
int sys_info_raid_impl(uint *blkn, uint *blks, uint *diskn);
int sys_destroy_raid_impl();
#endif //XV6_RISCV_OS2_RSICV_RAID_RAID_H
