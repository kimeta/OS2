#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "raid.h"
#include "fs.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_init_raid(void){
    int raid_level;
    argint(0, &raid_level);
    return sys_init_raid_impl(raid_level);
}

uint64 sys_read_raid(void){

    int blkNum;
    uint64 addr;
    uchar buffer[BSIZE];
    argint(0, &blkNum);
    argaddr(1, &addr);
    int return_val = sys_read_raid_impl(blkNum, buffer);
    copyout(myproc()->pagetable, addr, (char*) buffer, BSIZE);
    return return_val;
}

uint64 sys_write_raid(void) {

    int blkNum;
    uint64 addr;
    uchar buffer[BSIZE];
    argint(0, &blkNum);
    argaddr(1, &addr);
    copyin(myproc()->pagetable, (char*) buffer, addr, BSIZE);
    return sys_write_raid_impl(blkNum,  buffer);
}

uint64 sys_disk_fail_raid(void){
     int diskNum;
     argint(0, &diskNum);
     return sys_disk_fail_raid_impl(diskNum);
}

uint64 sys_disk_repaired_raid(void){
    int diskNum;
    argint(0, &diskNum);
    return sys_disk_repaired_raid_impl(diskNum);
}

uint64 sys_info_raid(void){
    uint64 blkNum;
    uint64 blkSize;
    uint64 diskNum;
    uint arg0, arg1, arg2;
    argaddr(0, &blkNum);
    argaddr(1, &blkSize);
    argaddr(2, &diskNum);
    int return_val = sys_info_raid_impl(&arg0, &arg1, &arg2);
    copyout(myproc()->pagetable, blkNum, (char*) &arg0, sizeof(uint) );
    copyout(myproc()->pagetable, blkSize, (char*) &arg1, sizeof(uint) );
    copyout(myproc()->pagetable, diskNum, (char*) &arg2, sizeof(uint) );

    return return_val;
}

uint64 sys_destroy_raid(void){return sys_destroy_raid_impl();}

