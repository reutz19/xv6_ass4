#define BOOTSTRAP 446

#define NPARTITIONS 4

#define PART_ALLOCATED	1	// allocated partition
#define PART_BOOTABLE	2	// bootable partition
#define PART_BOTH       3   // allocated and bootable 

#define FS_INODE		0 	// inode based partition
#define FS_FAT	 		1	// fat based partition

/* 16 bytes */
struct dpartition {
	uint flags;
	uint type;	
	uint offset;
	uint size;
};

#pragma pack(1)				// prevents the compiler from aligning (padding) generated code for 4 byte boundary
struct mbr {
	uchar bootstrap[BOOTSTRAP];
	struct dpartition partitions[NPARTITIONS];
	uchar magic[2];
}; /* 512 bytes */
	
struct partition {
	uint dev;
	uint flags;	
	uint type;	
	uint offset;
	uint size;
};

