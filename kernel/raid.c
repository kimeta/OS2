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
   int failed[RAID_DISK_NUMBER + 1]; // 0 false, 1 true
   int booted; // 0 false, 1 true
}raid_data;
//static struct raid raid_data;

uchar parity_buffer[BSIZE];

void copy_disk_data(int src, int dst){
    uchar buff[BSIZE];

    for(int i = 0; i < raid_data.numberOfBlocks / RAID_DISK_NUMBER; i++){
        read_block(src, i, buff);
        write_block(dst, i, buff);
    }

}

int sys_init_raid_impl(enum RAID_TYPE raid){
    if(raid_data.booted) return -1;

    raid_data.type = raid;
    raid_data.booted = 1;
    for(int i = 0; i <= RAID_DISK_NUMBER; i++)
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
            raid_data.numberOfBlocks = (RAID_DISK_NUMBER - 1) * VIRTIO_RAID_DISK_SIZE / BSIZE;
            break;
        case RAID5:
            raid_data.numberOfBlocks = (RAID_DISK_NUMBER - 1) * VIRTIO_RAID_DISK_SIZE / BSIZE;
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
            int i = 1;
            for(; i <= VIRTIO_RAID_DISK_END; i++){
                if(!raid_data.failed[i]) break;
            }
            if(i == RAID_DISK_NUMBER) return -1;
            read_block(i, blkn, data);
            break;
        case RAID0_1:
            diskNum = (blkn % (RAID_DISK_NUMBER / 2)) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER / 2);
            if(raid_data.failed[diskNum]){
                diskNum = diskNum + RAID_DISK_NUMBER / 2;
                if(raid_data.failed[diskNum]){
                    return -1;
                }
            }
            read_block(diskNum, blkNum, data);
            break;
        case RAID4:
            diskNum = (blkn % (RAID_DISK_NUMBER - 1)) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]) {
                uchar temp_parity_buffer[BSIZE];
                for (int i = 1; i < RAID_DISK_NUMBER; i++) {
                    uchar data_buffer[BSIZE];
                    if (i == diskNum)
                        continue;
                    read_block(i, blkNum, data_buffer);
                    for (int j = 0; j < BSIZE; j++) {
                        temp_parity_buffer[j] = temp_parity_buffer[j] ^ data_buffer[j];
                    }
                }
                read_block(RAID_DISK_NUMBER, blkNum, parity_buffer);
                for (int j = 0; j < BSIZE; j++) {
                    temp_parity_buffer[j] = temp_parity_buffer[j] ^ parity_buffer[j];
                }
                data = temp_parity_buffer;
                break;
            }
            read_block(diskNum, blkNum, data);
            break;
        case RAID5:

            diskNum = (blkn + blkn / RAID_DISK_NUMBER + 1) % (RAID_DISK_NUMBER) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]){
                memset(data, 0, BSIZE);
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    if(diskNum == i) continue;
                    uchar temporary_buffer[BSIZE];
                    read_block(i, blkNum, temporary_buffer);
                    for(int j = 0; j < BSIZE; j++){
                        data[j] = data[j] ^ temporary_buffer[j];
                    }
                }
                break;
            }
            read_block(diskNum, blkNum, data);
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
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++){
                if(raid_data.failed[i]) {
                    continue;
                }

                write_block(i, blkn, data);
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
            diskNum = blkn % (RAID_DISK_NUMBER - 1) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]) return -1;
            write_block(diskNum, blkNum, data);
            for(int i = 1; i < RAID_DISK_NUMBER; i++){
                uchar data_buffer[BSIZE];
                if(i != diskNum)
                    read_block(i, blkNum, data_buffer);
                else
                    for(int j = 0; j < BSIZE; j++)
                        data_buffer[j] = data[j];
                for(int j = 0; j < BSIZE; j++){
                    parity_buffer[j] = parity_buffer[j] ^ data_buffer[j];
                }
            }
            write_block(VIRTIO_RAID_DISK_END, blkNum, parity_buffer);
            break;
        case RAID5:
            diskNum = (blkn + blkn / RAID_DISK_NUMBER + 1) % (RAID_DISK_NUMBER) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]) return -1;
            write_block(diskNum, blkNum, data);
            int parity_disk = (blkNum % RAID_DISK_NUMBER) + 1;
            uchar temp_parity_buffer[BSIZE];
            read_block(parity_disk, blkNum, temp_parity_buffer);
            for(int j = 0; j < BSIZE; j++){
                temp_parity_buffer[j] = temp_parity_buffer[j] ^ data[j];
            }
            write_block(parity_disk, blkNum, temp_parity_buffer);
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
    raid_data.failed[diskn] = 0;
    switch(raid_data.type){
        case RAID0:
            break;
        case RAID1:
            int i = 1;
            for(;i <= RAID_DISK_NUMBER; i++){
                if(i != diskn && !raid_data.failed[i])
                    break;
            }
            if(i == RAID_DISK_NUMBER) return -1;

            copy_disk_data(i, diskn);

            break;
        case RAID0_1:
            int first_second = diskn / (RAID_DISK_NUMBER / 2);
            int disk_ind;
            if(first_second == 0){
                disk_ind = diskn + (RAID_DISK_NUMBER / 2);
            }else{
                disk_ind = diskn - (RAID_DISK_NUMBER / 2);
            }
            if( raid_data.failed[disk_ind] ) return -1;
            printf("\n%d %d\n", diskn, disk_ind);
            copy_disk_data(disk_ind, diskn);

            break;
        case RAID4:
            for (int i = 0; i < raid_data.numberOfBlocks; i += raid_data.numberOfBlocks / (RAID_DISK_NUMBER - 1)) {
                read_block(RAID_DISK_NUMBER, i / (RAID_DISK_NUMBER - 1), parity_buffer);
                uchar temp_parity_buffer[BSIZE];
                for (int k = 0; k < BSIZE; k++) {
                    temp_parity_buffer[k] = temp_parity_buffer[k] ^ parity_buffer[k];
                }
                for(int j = 0; j < raid_data.numberOfBlocks / RAID_DISK_NUMBER; j++){
                    uchar data_buffer[BSIZE];
                    disk_ind = (i + j) % (RAID_DISK_NUMBER - 1) + 1;
                    if (disk_ind == diskn)
                        continue;
                    int blk_ind = (i + j) / (RAID_DISK_NUMBER - 1);
                    read_block(disk_ind, blk_ind, data_buffer);
                    for (int k = 0; k < BSIZE; k++) {
                        temp_parity_buffer[k] = temp_parity_buffer[k] ^ data_buffer[k];
                    }
                }
                write_block(diskn, i / RAID_DISK_NUMBER, temp_parity_buffer);
            }
            break;
        case RAID5:

            for(int i = 0; i < raid_data.numberOfBlocks / RAID_DISK_NUMBER + 1; i++) {
                memset(parity_buffer, 0, BSIZE);
                for (int j = 1; j < RAID_DISK_NUMBER + 1; j++) {
                    if (diskn == j) continue;
                    if(raid_data.failed[j]) return -1;
                    uchar temporary_buffer[BSIZE];
                    printf("\n%d %d\n", j, i);
                    read_block(j, i, temporary_buffer);
                    for (int k = 0; k < BSIZE; k++) {
                        parity_buffer[k] = parity_buffer[k] ^ temporary_buffer[k];
                    }
                }
                write_block(diskn, i, parity_buffer);
            }
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

