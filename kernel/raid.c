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

struct raid_lock{
    struct spinlock lock;
    int writers;
    int readers;
};

static struct raid{
    enum RAID_TYPE type;
   uint16 numberOfBlocks;
   int failed[RAID_DISK_NUMBER + 1]; // 0 false, 1 true
   int booted; // 0 false, 1 true
   struct raid_lock locks[RAID_DISK_NUMBER + 1];
}raid_data;
//static struct raid raid_data;

uchar parity_buffer[BSIZE];

void raid_lock_read_acquire(struct raid_lock* rl){
    acquire(&rl->lock);
    while(rl->writers > 0) sleep(rl, &rl->lock);
    rl->readers++;
    release(&rl->lock);
}

void raid_lock_read_release(struct raid_lock* rl){
    acquire(&rl->lock);
    rl->readers--;
    if(rl->readers == 0 ) wakeup(rl);
    release(&rl->lock);
}

void raid_lock_write_acquire(struct raid_lock* rl){
    acquire(&rl->lock);
    while(rl->writers > 0 || rl ->readers) sleep(rl, &rl->lock);
    rl->writers++;
    release(&rl->lock);
}

void raid_lock_write_release(struct raid_lock* rl){
    acquire(&rl->lock);
    rl->writers--;
    wakeup(rl);
    release(&rl->lock);
}

void copy_disk_data(int src, int dst){
    uchar buff[BSIZE];
    raid_lock_read_acquire(&raid_data.locks[src]);
    raid_lock_write_acquire(&raid_data.locks[dst]);
    for(int i = 0; i < raid_data.numberOfBlocks / RAID_DISK_NUMBER; i++){
        read_block(src, i, buff);
        write_block(dst, i, buff);
    }
    raid_lock_read_release(&raid_data.locks[src]);
    raid_lock_write_release(&raid_data.locks[dst]);

}

int sys_init_raid_impl(enum RAID_TYPE raid){
    if(raid_data.booted) return -1;

    raid_data.type = raid;
    raid_data.booted = 1;
    for(int i = 1; i <= RAID_DISK_NUMBER; i++)
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
    char name[] = "lock0";
    for(int i = 1; i <= RAID_DISK_NUMBER; i++){
        name[4] = '0' + i;
        initlock(&raid_data.locks[i].lock, name);
        raid_data.locks[i].writers = 0;
        raid_data.locks[i].readers = 0;
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
            raid_lock_read_acquire(&raid_data.locks[diskNum]);
            if(raid_data.failed[diskNum]) {
                raid_lock_read_release(&raid_data.locks[diskNum]);
                return -1;
            }
            raid_lock_read_release(&raid_data.locks[diskNum]);
            read_block(diskNum, blkNum, data);
            break;
        case RAID1:
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_read_acquire(&raid_data.locks[i]);
            }
            int i = 1;
            for(; i <= VIRTIO_RAID_DISK_END; i++){
                if(!raid_data.failed[i]) break;
            }
            if(i == RAID_DISK_NUMBER) {
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    raid_lock_read_release(&raid_data.locks[i]);
                }
                return -1;
            }
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_read_release(&raid_data.locks[i]);
            }
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
            raid_lock_read_acquire(&raid_data.locks[diskNum]);
            read_block(diskNum, blkNum, data);
            raid_lock_read_release(&raid_data.locks[diskNum]);
            break;
        case RAID4:
            diskNum = (blkn % (RAID_DISK_NUMBER - 1)) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]){
                memset(data, 0, BSIZE);
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    raid_lock_read_acquire(&raid_data.locks[i]);
                }
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    if(diskNum == i) continue;
                    uchar temporary_buffer[BSIZE];
                    raid_lock_read_acquire(&raid_data.locks[i]);
                    read_block(i, blkNum, temporary_buffer);
                    raid_lock_read_release(&raid_data.locks[i]);
                    for(int j = 0; j < BSIZE; j++){
                        data[j] = data[j] ^ temporary_buffer[j];
                    }
                }
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    raid_lock_read_release(&raid_data.locks[i]);
                }
                break;
            }
            raid_lock_read_acquire(&raid_data.locks[diskNum]);
            read_block(diskNum, blkNum, data);
            raid_lock_read_release(&raid_data.locks[diskNum]);
            break;
        case RAID5:

            diskNum = (blkn + blkn / RAID_DISK_NUMBER + 1) % (RAID_DISK_NUMBER) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            if(raid_data.failed[diskNum]){
                memset(data, 0, BSIZE);
                for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                    for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                        raid_lock_read_acquire(&raid_data.locks[i]);
                    }
                    if(diskNum == i) continue;
                    uchar temporary_buffer[BSIZE];
                    raid_lock_read_acquire(&raid_data.locks[i]);
                    read_block(i, blkNum, temporary_buffer);
                    raid_lock_read_release(&raid_data.locks[i]);
                    for(int j = 0; j < BSIZE; j++){
                        data[j] = data[j] ^ temporary_buffer[j];
                    }
                    for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                        raid_lock_read_release(&raid_data.locks[i]);
                    }
                }
                break;
            }
            raid_lock_read_acquire(&raid_data.locks[diskNum]);
            read_block(diskNum, blkNum, data);
            raid_lock_read_release(&raid_data.locks[diskNum]);
            break;
    }
    return 0;
}

int sys_write_raid_impl(int blkn, uchar* data){

    if(!raid_data.booted || blkn >= raid_data.numberOfBlocks) return -1;
    int diskNum, blkNum, parity_disk;
    switch(raid_data.type){
        case RAID0:
            diskNum = (blkn % RAID_DISK_NUMBER) + 1;
            blkNum = blkn / RAID_DISK_NUMBER;
            raid_lock_write_acquire(&raid_data.locks[diskNum]);
            write_block(diskNum, blkNum, data);
            raid_lock_write_release(&raid_data.locks[diskNum]);
            break;
        case RAID1:
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_acquire(&raid_data.locks[i]);
            }
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++){
                if(raid_data.failed[i]) {
                    continue;
                }

                write_block(i, blkn, data);
            }
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_release(&raid_data.locks[i]);
            }
            break;
        case RAID0_1:
            int diskNum1 = (blkn % (RAID_DISK_NUMBER / 2)) + 1;
            int diskNum2 = diskNum1 + RAID_DISK_NUMBER / 2;
            int blkNum = blkn / (RAID_DISK_NUMBER / 2);
            int count = 0;
            if(!raid_data.failed[diskNum1]){
                count = 1;
                raid_lock_write_acquire(&raid_data.locks[diskNum1]);
                write_block(diskNum1, blkNum, data);
                raid_lock_write_release(&raid_data.locks[diskNum1]);
            }
            if(!raid_data.failed[diskNum2]){
                raid_lock_write_acquire(&raid_data.locks[diskNum2]);
                write_block(diskNum2, blkNum, data);
                raid_lock_write_release(&raid_data.locks[diskNum2]);
                count = 1;
            }
            if(count == 0) return -1;
            break;
        case RAID4:
            diskNum = blkn % (RAID_DISK_NUMBER - 1) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            parity_disk = RAID_DISK_NUMBER;
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_acquire(&raid_data.locks[i]);
            }
            if(raid_data.failed[diskNum]) {
                if(raid_data.failed[parity_disk]) {
                    for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                        raid_lock_write_release(&raid_data.locks[i]);
                    }
                    return -1;
                }
                uchar temp_parity_buffer[BSIZE];
                read_block(parity_disk, blkNum, temp_parity_buffer);
                for (int j = 0; j < BSIZE; j++) {
                    temp_parity_buffer[j] = temp_parity_buffer[j] ^ data[j];
                }
                write_block(parity_disk, blkNum, temp_parity_buffer);
            }else{
                uchar old_val[BSIZE];
                write_block(diskNum, blkNum, data);
                if(raid_data.failed[parity_disk]) {
                    for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                        raid_lock_write_release(&raid_data.locks[i]);
                    }
                    return -1;
                }
                read_block(diskNum, blkNum, old_val);
                read_block(parity_disk, blkNum, parity_buffer);
                for (int j = 0; j < BSIZE; j++) {
                    parity_buffer[j] = parity_buffer[j] ^ data[j] ^ old_val[j];
                }
                write_block(parity_disk, blkNum, parity_buffer);
            }
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_release(&raid_data.locks[i]);
            }
            break;
        case RAID5:
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_acquire(&raid_data.locks[i]);
            }
            diskNum = (blkn + blkn / RAID_DISK_NUMBER + 1) % (RAID_DISK_NUMBER) + 1;
            blkNum = blkn / (RAID_DISK_NUMBER - 1);
            parity_disk = (blkNum % RAID_DISK_NUMBER) + 1;
            if(raid_data.failed[diskNum]) {
                uchar temp_parity_buffer[BSIZE];
                read_block(parity_disk, blkNum, temp_parity_buffer);
                for (int j = 0; j < BSIZE; j++) {
                    temp_parity_buffer[j] = temp_parity_buffer[j] ^ data[j];
                }
                write_block(parity_disk, blkNum, temp_parity_buffer);
            }else{
                uchar old_val[BSIZE];
                read_block(diskNum, blkNum, old_val);
                write_block(diskNum, blkNum, data);
                read_block(parity_disk, blkNum, parity_buffer);
                for (int j = 0; j < BSIZE; j++) {
                    parity_buffer[j] = parity_buffer[j] ^ data[j] ^ old_val[j];
                }
                write_block(parity_disk, blkNum, parity_buffer);
            }
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_release(&raid_data.locks[i]);
            }
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
            return -1;
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
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_acquire(&raid_data.locks[i]);
            }
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
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_release(&raid_data.locks[i]);
            }
            break;
        case RAID5:
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_acquire(&raid_data.locks[i]);
            }
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
            for(int i = 1; i < RAID_DISK_NUMBER + 1; i++){
                raid_lock_write_release(&raid_data.locks[i]);
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