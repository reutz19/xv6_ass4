#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"
#include "mbr.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;  
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;
struct mbr mbr;     // mbr block

void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

uint currpr_offset;

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

void
write_kernel(char* kernel_file)
{
  char buf[BSIZE];

  // Open kernel file
  int kernel_fd = open(kernel_file, O_RDONLY, 0666);
  if(kernel_fd < 0){
    perror(kernel_file);
    exit(1);
  }

  // Get kernel size (bytes)
  /*off_t fsize;
  fsize = lseek(kernel_fd, 0, SEEK_END);
  lseek(kernel_fd, 0, SEEK_SET);
  */
  // Copy block after block
  //int kernel_size = fsize;
  //int copy_size = (kernel_size > BSIZE) ? BSIZE : kernel_size;
  memset(buf, 0, BSIZE);
    
  while (read(kernel_fd, buf, BSIZE) > 0) 
  {
    wsect(0, buf);    // 0 is relative to currpr_offset
    //kernel_size -= copy_size;
    // Determine size to copy
    //copy_size = (kernel_size > BSIZE) ? BSIZE : kernel_size;
    memset(buf, 0, BSIZE);
    
    // Update next block and size left
    ++currpr_offset;
  }
  close(kernel_fd);
}

int
main(int argc, char *argv[])
{
  int i, j, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;

  printf("--------------- OUR MKFS ------------------- \n");
  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 4){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  //read bootblock
  int bootbl_fd = open(argv[2], O_RDONLY|O_CREAT, 0666);
  if(bootbl_fd < 0){
    perror(argv[2]);
    exit(1);
  }

  currpr_offset = 0;
  //zero all partition
  for(i = 0; i < FSSIZE*5; i++)
    wsect(i, zeroes);

  //write boot code into mbr.bootstrap and close its fd
  memset(mbr.bootstrap, 0, BOOTSTRAP);
  read(bootbl_fd, mbr.bootstrap, BOOTSTRAP);
  close(bootbl_fd);

  currpr_offset = 1;
  // read kernel and write it to the disk
  write_kernel(argv[3]);

  printf("mkfs: Wrote KERNEL \n");
  //set mbr magic numbers
  mbr.magic[0] = 0x55;                    
  mbr.magic[1] = 0xAA;                

  nmeta = 1 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;
  //uint currpr_offset = last_kernel_block + 1;

  struct dpartition* curr_partition;
  int sh_exist, init_exist;
  char* argv_tmp;
  //set sb for each partition
  for(j = 0; j < NPARTITIONS; j++) 
  {
    freeinode = 1;
    printf("partition %d allocated \n", j);
    //setup partition j
    curr_partition = &(mbr.partitions[j]);
    curr_partition->flags  = PART_ALLOCATED;
    curr_partition->type   = -1; //FS_INODE;
    curr_partition->offset = currpr_offset;
    curr_partition->size   = FSSIZE;

    //setup superblock j
    sb.size = xint(FSSIZE);
    sb.nblocks = xint(nblocks);
    sb.ninodes = xint(NINODES);
    sb.nlog = xint(nlog);
    sb.logstart = xint(1);
    sb.inodestart = xint(1 + nlog);
    sb.bmapstart = xint(1 + nlog + ninodeblocks); 
    
    //write partition sb to the start of the partition => currpr_offset
    memset(buf, 0, BSIZE);
    memmove(buf, &sb, BSIZE);
    wsect(0, buf);              // 0 relative to currpr_offset

    freeblock = nmeta;          // the first free block that we can allocate

    printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
            nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

    rootino = ialloc(T_DIR);
    assert(rootino == ROOTINO);

    bzero(&de, sizeof(de));
    de.inum = xshort(rootino);
    strcpy(de.name, ".");
    iappend(rootino, &de, sizeof(de));

    bzero(&de, sizeof(de));
    de.inum = xshort(rootino);
    strcpy(de.name, "..");
    iappend(rootino, &de, sizeof(de));

    sh_exist = 0;
    init_exist = 0;

    for(i = 4; i < argc; i++){
      assert(index(argv[i], '/') == 0);

      if((fd = open(argv[i], 0)) < 0){
        perror(argv[i]);
        exit(1);
      }
      
      // Skip leading _ in name when writing to file system.
      // The binaries are named _rm, _cat, etc. to keep the
      // build operating system from trying to execute them
      // in place of system binaries like rm and cat.
      argv_tmp = argv[i];
      if(argv_tmp[0] == '_') 
        argv_tmp++;

      printf("mkfs: alloc %s\n", argv[i]);
      inum = ialloc(T_FILE);

      bzero(&de, sizeof(de));
      de.inum = xshort(inum);
      strncpy(de.name, argv_tmp, DIRSIZ);
      iappend(rootino, &de, sizeof(de));

      if (strlen(argv_tmp) == 2 && strncmp(argv_tmp, "sh", 2) == 0){
          printf("mkfs: sh exist \n");
          sh_exist = 1;
      }
      else if (strlen(argv_tmp) == 4 && strncmp(argv_tmp, "init", 4) == 0) {
        printf("mkfs: init exist \n");
        init_exist = 1;
      }

      memset(buf,0, sizeof(buf));
      while((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);

      close(fd);
    } // end inside for loop

    if (sh_exist && init_exist) {
      printf("partition number:%d, sh_exist && init_exist\n",j);
      curr_partition->flags = PART_BOTH;
      curr_partition->type  = FS_INODE;
    }
      
    // fix size of root inode dir
    rinode(rootino, &din);
    off = xint(din.size);
    off = ((off/BSIZE) + 1) * BSIZE;
    din.size = xint(off);
    winode(rootino, &din);

    balloc(freeblock);
    currpr_offset += FSSIZE;    // next partition
  }// end outside for loop

  currpr_offset = 0;
  // write mbr to the disk block 0
  memset(buf, 0, sizeof(buf));
  memmove(buf, &mbr, sizeof(mbr));
  wsect(0, buf);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  struct dpartition* curr_partition;

  int j;
  for(j = 0; j < NPARTITIONS; j++) 
  {
    curr_partition = &(mbr.partitions[j]);
    printf("partition %d curr_partition->flags=%d currpr_offset=%d\n", j, curr_partition->flags, currpr_offset);
  }

  if(lseek(fsfd, (sec + currpr_offset) * BSIZE, 0) != (sec + currpr_offset) * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, (sec + currpr_offset) * BSIZE, 0) != (sec + currpr_offset) * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
