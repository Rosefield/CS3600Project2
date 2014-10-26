/*
 * CS3600, Spring 2014
 * Project 2 Starter Code
 * (c) 2013 Alan Mislove
 *
 * This program is intended to format your disk file, and should be executed
 * BEFORE any attempt is made to mount your file system.  It will not, however
 * be called before every mount (you will call it manually when you format 
 * your disk file).
 */

#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>

#include "inode.h"
#include "3600fs.h"
#include "disk.h"

void myformat(int size) {
  // Do not touch or move this function
  dcreate_connect();

  /* 3600: FILL IN CODE HERE.  YOU SHOULD INITIALIZE ANY ON-DISK
           STRUCTURES TO THEIR INITIAL VALUE, AS YOU ARE FORMATTING
           A BLANK DISK.  YOUR DISK SHOULD BE size BLOCKS IN SIZE. */
	
	//Initialize and write VCB
	vcb myvcb;
	memset(&myvcb, 0, sizeof(vcb));

	myvcb.magic = 0x00000077;
	myvcb.blocksize = BLOCKSIZE;
	myvcb.root = (blocknum){.block=1, .valid=1};
	myvcb.free = (blocknum){.block=3, .valid=1};			
	strcpy(myvcb.name, "Why am I here?");
	myvcb.dirty = 0;	

	char tmp[BLOCKSIZE];
	memcpy(tmp, &myvcb, sizeof(vcb));
	dwrite(0, tmp);

	//Initialize and write root DNODE
	dnode mydnode;
	memset(&mydnode, 0, sizeof(dnode));

	mydnode.size = 2;
	mydnode.user = getuid();
	mydnode.group = getgid();
	mydnode.mode = S_IRWXU | S_IRWXG | S_IRWXO;

	clock_gettime(CLOCK_REALTIME, &mydnode.access_time);
	clock_gettime(CLOCK_REALTIME, &mydnode.modify_time);
	clock_gettime(CLOCK_REALTIME, &mydnode.create_time);
	
	mydnode.direct[0] = (blocknum){.block=2, .valid=1};
	//Technically memset to zero first, but do it again, for extra redundancy.
	mydnode.direct[1].valid = 0;
	mydnode.single_indirect.valid = 0;
	mydnode.double_indirect.valid = 0;

	memcpy(tmp, &mydnode, sizeof(mydnode));
	dwrite(1, tmp);

	//Write DIRENT
	dirent mydirent;
	memset(&mydirent, 0, sizeof(dirent));
	mydirent.entries[0] = (direntry){".", DIRENTRY_DIR, {1,1}};
	mydirent.entries[1] = (direntry){"..", DIRENTRY_DIR, {1,1}};
	
	memcpy(tmp, &mydirent, sizeof(dirent));
	dwrite(2, tmp);

	//Initialize FREE blocks
    free_block myfree;
	memset(&myfree, 0, sizeof(free_block));
	
	for (int i=3; i<size -1; i++) {
		//Extra verbose just in case sizeof(free) gives the size of the free() function
		myfree.next = (blocknum){i+1, 1};
	
		memcpy(tmp, &myfree, sizeof(free_block));
		if (dwrite(i, tmp) < 0) {
    	  	perror("Error while writing to disk");
		}
	}
	//Last block should have an invalid blocknum (lsb = 0) for next
	myfree.next = (blocknum){0, 0};
	memcpy(tmp, &myfree, sizeof(free_block));
	dwrite(size - 1, tmp);

  // Do not touch or move this function
  dunconnect();
}

int main(int argc, char** argv) {
  // Do not touch this function
  if (argc != 2) {
    printf("Invalid number of arguments \n");
    printf("usage: %s diskSizeInBlockSize\n", argv[0]);
    return 1;
  }

  unsigned long size = atoi(argv[1]);
  printf("Formatting the disk with size %lu \n", size);
  myformat(size);
}
