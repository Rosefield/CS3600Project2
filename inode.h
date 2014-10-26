#include <sys/types.h>
#include <unistd.h>
#include <time.h>

typedef struct blocknum_t {
	unsigned int block:31;
	unsigned int valid:1;
} blocknum;

typedef struct vcb_t{
	//Magic is 0x00000077
	int magic; // 4

	int blocksize; // 4

	blocknum root; // 4

	blocknum free; // 4

	char dirty;

	char name[495]; // 496 * 1
} vcb;


typedef struct dnode_t{
	unsigned int size; //4
	uid_t user; // 4
	gid_t group; // 4
	mode_t mode; // 4

	struct timespec access_time;  // 16
	struct timespec modify_time;  // 16 
	struct timespec create_time; // 16
	
	blocknum direct[110]; // 110 * 4
	blocknum single_indirect; // 4
	blocknum double_indirect; // 4
} dnode;

typedef struct indirect_t {
	blocknum blocks[128]; // 128 * 4
} indirect;

#define DIRENTRY_FILE 1
#define DIRENTRY_DIR  2

typedef struct direntry_t {
	char name[59]; //27 * 1
	char type; //1
	blocknum block; //4
} direntry; //32

#define ENTRIES_IN_DIR 8

typedef struct dirent_t {
	direntry entries[ENTRIES_IN_DIR]; // 32 * 16 = 512
} dirent;

//Dnode and Inode share the same structure
typedef dnode inode;

typedef struct free_t {
	//Either points to the next block, or null if there are no longer and free blocks
	blocknum next; // 4
	//Junk data to make free take up 512bytes of space
	char filler[508]; // 508 * 1
} free_block;

typedef struct {
    unsigned int blocknum;
    char data[512];
    // TODO: add dirty bit to tell if need to write to disk upon eviction
} cache_ent;

typedef struct {
    cache_ent entries[3];
} cache;
