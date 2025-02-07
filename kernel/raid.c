#include "types.h"
#include "virtio.h"
#include "raid.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"
#include "riscv.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"
#define RAID_DISK_NUMBER (VIRTIO_RAID_DISK_END)

static struct raid{
    enum RAID_TYPE type;
   uint16 numberOfBlocks;
   int failed[RAID_DISK_NUMBER]; // 0 false, 1 true
   int booted; // 0 false, 1 true
}raid_data;
//static struct raid raid_data;

void copy_disk_data(int src, int dst){
    uchar buff[BSIZE];
    for(int i = 0; i < raid_data.numberOfBlocks; i++){
        read_block(src, i, buff);
        write_block(dst, i, buff);
    }
}

int sys_init_raid_impl(enum RAID_TYPE raid){
    if(raid_data.booted) return -1;

    raid_data.type = raid;
    raid_data.booted = 1;
    for(int i = 0; i < RAID_DISK_NUMBER; i++)
        raid_data.failed[i] = 0;
    switch(raid){
        case RAID0:
            raid_data.numberOfBlocks = RAID_DISK_NUMBER * VIRTIO_RAID_DISK_SIZE / BSIZE;
            break;
        case RAID1:
            raid_data.numberOfBlocks = VIRTIO_RAID_DISK_SIZE / BSIZE;
            break;
        case RAID0_1:
            raid_data.numberOfBlocks = RAID_DISK_NUMBER / 2 * VIRTIO_RAID_DISK_SIZE / BSIZE;
            break;
        case RAID4:
            break;
        case RAID5:
            break;
    }
    return 0;
}
int sys_read_raid_impl(int blkn, uchar* data){
    if(!raid_data.booted || blkn >= raid_data.numberOfBlocks) return -1;
    int diskNum, blkNum;
    switch(raid_data.type){
        case RAID0:
            diskNum = (blkn % RAID_DISK_NUMBER) + 1;
            blkNum = blkn / RAID_DISK_NUMBER;
            if(raid_data.failed[diskNum]) return -11;
            read_block(diskNum, blkNum, data);
            break;
        case RAID1:
            int i = 0;
            for(; i < RAID_DISK_NUMBER; i++){
                if(!raid_data.failed[i]) break;
            }
            if(i == RAID_DISK_NUMBER) return -1;
            read_block(i, blkn, data);
            break;
        case RAID0_1:
            diskNum = (blkn % (RAID_DISK_NUMBER / 2)) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER / 2);
            if(raid_data.failed[diskNum]){
                if(raid_data.failed[diskNum + RAID_DISK_NUMBER / 2]){
                    return -1;
                }
                diskNum = diskNum + RAID_DISK_NUMBER / 2;
            }
            read_block(diskNum, blkNum, data);
            break;
        case RAID4:
            break;
        case RAID5:
            break;
    }
    return 0;
}
int sys_write_raid_impl(int blkn, uchar* data){

    if(!raid_data.booted || blkn >= raid_data.numberOfBlocks) return -1;
    int diskNum, blkNum;
    switch(raid_data.type){
        case RAID0:
            diskNum = (blkn % RAID_DISK_NUMBER) + 1;
            blkNum = blkn / RAID_DISK_NUMBER;
            if(raid_data.failed[diskNum]) return -1;
            write_block(diskNum, blkNum, data);
            break;
        case RAID1:
            for(int i = 0; i < RAID_DISK_NUMBER; i++){
                if(raid_data.failed[i]) continue;

                write_block(i, blkn, data);
                printf("ALO");
            }
            break;
        case RAID0_1:
            int diskNum1 = (blkn % (RAID_DISK_NUMBER / 2)) + 1;
            int diskNum2 = diskNum1 + RAID_DISK_NUMBER / 2;
            int blkNum = blkn / (RAID_DISK_NUMBER / 2);
            int count = 0;
            if(!raid_data.failed[diskNum1]){
                count = 1;
                write_block(diskNum1, blkNum, data);
            }
            if(!raid_data.failed[diskNum2]){
                write_block(diskNum2, blkNum, data);
                count = 1;
            }
            if(count == 0) return -1;
            break;
        case RAID4:
            break;
        case RAID5:
            break;
    }

    return 0;
}
int sys_disk_fail_raid_impl(int diskn){
    if(!raid_data.booted || diskn > VIRTIO_RAID_DISK_END || diskn < VIRTIO_RAID_DISK_START || raid_data.failed[diskn]) return -1;
    raid_data.failed[diskn] = 1;
    return 0;
}
int sys_disk_repaired_raid_impl(int diskn){
    if(!raid_data.booted || diskn > VIRTIO_RAID_DISK_END || diskn < VIRTIO_RAID_DISK_START || !raid_data.failed[diskn]) return -1;
    switch(raid_data.type){
        case RAID0:
            raid_data.failed[diskn] = 0;
            break;
        case RAID1:
            raid_data.failed[diskn] = 0;
            int i = 0;
            for(;i < RAID_DISK_NUMBER; i++){
                if(i != diskn && !raid_data.failed[i])
                    break;
            }
            if(i == RAID_DISK_NUMBER) return -1;
            copy_disk_data(i, diskn);
            break;
        case RAID0_1:
            raid_data.failed[diskn] = 0;
            int first_second = diskn / (RAID_DISK_NUMBER / 2);
            int disk_ind;
            if(first_second == 0){
                disk_ind = diskn + (RAID_DISK_NUMBER / 2);
            }else{
                disk_ind = diskn - (RAID_DISK_NUMBER / 2);
            }
            if( raid_data.failed[disk_ind] ) return -1;
            copy_disk_data(disk_ind, diskn);
            break;
        case RAID4:
            break;
        case RAID5:
            break;
    }
    return 0;
}
int sys_info_raid_impl(uint *blkn, uint *blks, uint *diskn){
    if(!raid_data.booted) return -1;
    *diskn = RAID_DISK_NUMBER;
    *blks = BSIZE;
    *blkn = raid_data.numberOfBlocks;
    return 0;
}
int sys_destroy_raid_impl(){
    raid_data.booted = 0;
    return 0;
}

