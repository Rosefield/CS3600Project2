/*
 * CS3600, Spring 2014
 * Project 2 Starter Code
 * (c) 2013 Alan Mislove
 *
 * This file contains all of the basic functions that you will need
 * to implement for this project.  Please see the project handout
 * for more details on any particular function, and ask on Piazza if
 * you get stuck.
 */

#define FUSE_USE_VERSION 26

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define _POSIX_C_SOURCE 199309

#include <time.h>
#include <fuse.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <sys/statfs.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "3600fs.h"
#include "disk.h"
#include "inode.h"

vcb head;
cache *c;

/*
 * Cache Functions
 */

/*
 * cdread - cache wrapper over dread
 */
int cdread(int blocknum, char *buf) {

    /* only cache vcb, root dnode, root dirent */
    if (blocknum >= 0 && blocknum <= 2) {
        memcpy(buf, c->entries[blocknum].data, BLOCKSIZE);
        return BLOCKSIZE;
    } else {
        return dread(blocknum, buf);
    }

    return -1;
}


/*
 * cdwrite - cache wrapper over dwrite
 */
int cdwrite(int blocknum, char *buf){
    if (blocknum >= 0 && blocknum <= 2) {
        memcpy(c->entries[blocknum].data, buf, BLOCKSIZE);
        return BLOCKSIZE;
    } else {
        return dwrite(blocknum, buf);
    }

    return -1;
}

/*
 * Helper Functions
 */

/*
 * name_in_dirent - search dirent for filename
 *
 * Utility function used by file_exists.
 *
 * d is the dirent to search in,
 * name is the value to search for
 * de is a return value, if this is non-null it will copy the direntry for the found file
 * returns 1 if the file is found, 0 otherwise
 */
int name_in_dirent(dirent d, char * name, direntry * de) {
	for(int x = 0; x < ENTRIES_IN_DIR; ++x) {
		if(strcmp(d.entries[x].name, name) == 0) {
			if(de) {
				*de = d.entries[x];
			}
			return 1;
		}
	}
	return 0;
}

/*
 * Updates the VCB to be the next free block
 */
void vcb_update_free() {
	free_block tmp;

	cdread(head.free.block, (char *)&tmp);

	//Initialize the removed free block to 0s
	char data[BLOCKSIZE];
	memset(data, 0, BLOCKSIZE);
	cdwrite(head.free.block, data);

    head.free = tmp.next;
    cdwrite(0, (char*) &head);
}

/*
 * get_node_block - Returns blocknum of data block at offset. Goes through
 * direct, indirect, and double_indirect blocks
 *
 * @node: the node to read from
 * @offset: node block index
 */
blocknum get_node_block(inode * node, unsigned int offset) {

	//This function will return a "null" blocknumber {0, 0} if the blocknum it is supposed to read
	//or return is not valid

	//In triple indirect
	if(offset >= NUM_DIRECT + 128 + 128 * 128) {
		if(!node->triple_indirect.valid) {
			return (blocknum){0,0};
		}
		indirect triple_indirect;
		cdread(node->triple_indirect.block, (char *)&triple_indirect);

		offset -= NUM_DIRECT + 128 + 128 * 128;

		int triple_offset = offset / (128 * 128) % 128;

		if(!triple_indirect.blocks[triple_offset].valid) {
			return (blocknum){0,0};
		}

		indirect double_indirect;
		cdread(triple_indirect.blocks[triple_offset].block, (char *)&double_indirect);

		int double_offset = offset / 128 % 128;
		//If there doesn't exist a single_indirect block at the double_offset
		if(!double_indirect.blocks[double_offset].valid) {
			return (blocknum){0,0};
		}
		indirect single_indirect;
		cdread(double_indirect.blocks[double_offset].block, (char *)&single_indirect);

        /* get single indirect offset using normalized offset */
		int single_offset = offset % 128;
		return single_indirect.blocks[single_offset];

	}


	//In double indirect
	if(offset >= NUM_DIRECT + 128) {
		//No double indirect block
		if(!node->double_indirect.valid) {
			return (blocknum){0,0};
		}
		indirect double_indirect;
		cdread(node->double_indirect.block, (char *)&double_indirect);

		offset -= NUM_DIRECT + 128;

		int double_offset = offset / 128 % 128;
		//If there doesn't exist a single_indirect block at the double_offset
		if(!double_indirect.blocks[double_offset].valid) {
			return (blocknum){0,0};
		}
		indirect single_indirect;
		cdread(double_indirect.blocks[double_offset].block, (char *)&single_indirect);

        /* get single indirect offset using normalized offset */
		int single_offset = offset % 128;
		return single_indirect.blocks[single_offset];

	}

	//In single indirect
	if(offset >= NUM_DIRECT) {
		offset -= NUM_DIRECT;
		//No single indirect block
		if(!node->single_indirect.valid) {
			return (blocknum){0,0};
		}
		indirect single;
		cdread(node->single_indirect.block, (char *) &single);
		if(!single.blocks[offset].valid) {
			return (blocknum){0,0};
		}
		return single.blocks[offset];

	}

	if(!node->direct[offset].valid) {
		return (blocknum){0,0};
	}

	return node->direct[offset];
}

/**
* set_node_block - Given a node, finds the offset-th block in node, and sets
* it to block.
*
**/
void set_node_block(inode * node, blocknum inode_block, blocknum block, unsigned int offset) {

	//In triple indirect
	if(offset >= NUM_DIRECT + 128 + 128 * 128) {
		if(!node->triple_indirect.valid) {
			node->triple_indirect = head.free;
			vcb_update_free();
			cdwrite(inode_block.block, (char *)node);
		}
		indirect triple_indirect;
		cdread(node->triple_indirect.block, (char *)&triple_indirect);

		offset -= NUM_DIRECT + 128 + 128 * 128;

		int triple_offset = offset / (128 * 128) % 128;

		if(!triple_indirect.blocks[triple_offset].valid) {
			triple_indirect.blocks[triple_offset] = head.free;
			vcb_update_free();
			cdwrite(node->triple_indirect.block, (char *)&triple_indirect);
		}

		indirect double_indirect;
		cdread(triple_indirect.blocks[triple_offset].block, (char *)&double_indirect);

		int double_offset = offset / 128 % 128;
		//If there doesn't exist a single_indirect block at the double_offset
		if(!double_indirect.blocks[double_offset].valid) {
			double_indirect.blocks[double_offset] = head.free;
			vcb_update_free();
			cdwrite(triple_indirect.blocks[triple_offset].block, (char *)&double_indirect);
		}
		indirect single_indirect;
		cdread(double_indirect.blocks[double_offset].block, (char *)&single_indirect);

		int single_offset = offset % 128;
		single_indirect.blocks[single_offset] = block;
		cdwrite(double_indirect.blocks[double_offset].block, (char *) &single_indirect);
		return;
	}

	//In double indirect
	if(offset >= NUM_DIRECT + 128) {
		indirect double_indirect;
		//If the node doesn't currently have a double_indirect block allocate one
		if(!node->double_indirect.valid) {
			node->double_indirect = head.free;
			vcb_update_free();
			cdwrite(inode_block.block, (char *)node);
		}
		cdread(node->double_indirect.block, (char *)&double_indirect);
	
		offset -= NUM_DIRECT + 128;
		int double_offset = offset / 128 % 128;

		indirect single_indirect;
		//If there isn't a single indirect block at the offset, allocate it
		if(!double_indirect.blocks[double_offset].valid) {
			double_indirect.blocks[double_offset] = head.free;
			vcb_update_free();
			cdwrite(node->double_indirect.block, (char *)&double_indirect);
		}
		cdread(double_indirect.blocks[double_offset].block, (char *)&single_indirect);
	
		int single_offset = offset % 128;
		single_indirect.blocks[single_offset] = block;
		cdwrite(double_indirect.blocks[double_offset].block, (char *) &single_indirect);
		return;
	}

	//In single indirect
	if(offset >= NUM_DIRECT) {
		offset -= NUM_DIRECT;
		indirect single;
		//If the node doesn't have a single_indirect block, allocate it.
		if(!node->single_indirect.valid) {
			node->single_indirect = head.free;
			vcb_update_free();
			cdwrite(inode_block.block, (char *)node);
		}
		cdread(node->single_indirect.block, (char *) &single);
		single.blocks[offset] = block;
		cdwrite(node->single_indirect.block, (char *) &single);
		return;
	}
	
	node->direct[offset] = block;
	cdwrite(inode_block.block, (char *)node);
	return;
}

/**
*write_node_block - Takes a node, writes data to the offset-th block, or if that block
*doesn't exist, allocate it, write to it, and then set the offset-th block
*
**/
void write_node_block(inode * node, blocknum inode_block, char * data, unsigned int offset) {
	blocknum block = get_node_block(node, offset);
	//Get a free block from the vcb
	if(!block.valid) {
		block = head.free;
		if(!block.valid) {
		//No more free blocks!
			return;
		}
		vcb_update_free();
		//Update the node to contain the new block
		set_node_block(node, inode_block, block, offset);
	}

	cdwrite(block.block, data);
}

/*
 * file_exists - search directory for file
 *
 * de is an optional parameter, that when passed in, and is not null, will be set to the
 * direntry for the found file
 */
int file_exists(dnode * dir, char * name, direntry * de) {
	if(dir->size == 0) {
		return 0;
	}

	unsigned offset = 0;

	//Iterate over dirents in dir, check if they contain the name we are looking for
	for(; offset < MAX_BLOCKS; ++offset) {
		blocknum block = get_node_block(dir, offset);
		if(!block.valid) {
			return 0;
		}

		dirent tmp;
		cdread(block.block, (char *)&tmp);
		if(name_in_dirent(tmp, name, de)) {
			return 1;
		}
	}

	return 0;
}

//checks if the path exists, and returns the direntry for the last item if de != null
//So, if /a/b/c is passed in, and /a/b/c exists, de will be for c
//If the argument given for path is "/", then  de will not be set.
int path_exists(const char * path, direntry * de) {
	if(strcmp("/", path) == 0) {
		dirent d;
		cdread(2, (char *)&d);
		*de = d.entries[0];
		return 1;
	}

	dnode our_node;
	cdread(1, (char *)&our_node);
	//Path + 1 to removet the initial 
	char * name = strdup(path);
	char * backup = name;	

	if(name[0] == '/') {
		name = name + 1;
	}

	name = strtok(name, "/");

	direntry tmp;
	while(name) {
		if(!file_exists(&our_node, name, &tmp)) {
			free(backup);
			return 0;
		}
		cdread(tmp.block.block, (char *)&our_node);

		name = strtok(NULL, "/");
	}

	free(backup);
	*de = tmp;
	return 1;
}

/**
* add_direntry - Adds our_direntry to the first free (!block.valid) spot in dir
*
*/
void add_direntry(dnode * dir, unsigned int dir_block, direntry our_direntry) {
	unsigned offset = 0;

	for(; offset < MAX_BLOCKS; ++offset) {
		
		blocknum write_block = get_node_block(dir, offset);

		/* we get the first root dirent for free (from 3600mkfs) but afterwards
           we need to allocate dirents
           
           if we're starting a new dirent, its block will be 0 from the initial
           format
		*/
		if(!write_block.valid) {
			write_block = head.free;
			vcb_update_free();
			set_node_block(dir, (blocknum){dir_block, 1}, write_block, offset);
		}
		dirent tmp;
		cdread(write_block.block, (char *)&tmp);
		for(int i = 0; i < ENTRIES_IN_DIR; ++i) {
			if(!tmp.entries[i].block.valid) {
				tmp.entries[i] = our_direntry;
				cdwrite(write_block.block, (char *) &tmp);
				dir->size++;
				cdwrite(dir_block, (char *)dir);
				return;
			}
		}
	}
}

/*
 * release_block -- free an arbitrary block, given number. create new free
 * block pointing to current head.free and write it. then update head.free
 * to point to newly replaced block
 */
void release_block (blocknum block) {
    /* new free block, pointing to current head.free */
    free_block tmp;
    tmp.next = head.free;
    cdwrite(block.block, (char*) &tmp);
    /* set head.free to new */
    head.free = block;
    head.free.valid = 1;
	cdwrite(0, (char*) &head);
}

//Fills fields in the dnode, gets a free block for its first dirent
void init_dnode(dnode * node) {
	memset(node, 0, sizeof(dnode));

	node->size = 2;
	node->user = getuid();
	node->group = getgid();

	clock_gettime(CLOCK_REALTIME, &node->access_time);
	clock_gettime(CLOCK_REALTIME, &node->modify_time);
	clock_gettime(CLOCK_REALTIME, &node->create_time);
	
	node->direct[0] = head.free;
	vcb_update_free();
	//Technically memset to zero first, but do it again, for extra redundancy.
	node->direct[1].valid = 0;
	node->single_indirect.valid = 0;
	node->double_indirect.valid = 0;
}

void init_dirent(dirent * de, blocknum dnode_block) {
	memset(de, 0, sizeof(dirent));
	de->entries[0] = (direntry){".", DIRENTRY_DIR, dnode_block};
	de->entries[1] = (direntry){"..", DIRENTRY_DIR, dnode_block};
}

//Assumes that the last char is not a '/'
//gets the "file name"in a path, ie for path /a/b/c, returns c
char * file_name(const char * path) {
	char * file_name = malloc(strlen(path));
	strcpy(file_name, strrchr(path, '/') + 1);
	return file_name;
}

//Gets the full directory in a path, IE given /a/b/c return /a/b, or given /a/b/c/, /a/b/c
char * dir_name(const char * path) {
	const char * last_char = strrchr(path, '/');
	char * dir = malloc(strlen(path) - strlen(last_char) + 2);
	
	strncpy(dir, path, strlen(path) - strlen(last_char) + 1);
	dir[strlen(path) - strlen(last_char) + 1] = '\0';	
	return dir;
}


/*
 * Initialize filesystem. Read in file system metadata and initialize
 * memory structures. If there are inconsistencies, now would also be
 * a good time to deal with that. 
 *
 * HINT: You don't need to deal with the 'conn' parameter AND you may
 * just return NULL.
 *
 */
static void* vfs_mount(struct fuse_conn_info *conn) {
    fprintf(stderr, "vfs_mount called\n");

    // Do not touch or move this code; connects the disk
    dconnect();

    /* 3600: YOU SHOULD ADD CODE HERE TO CHECK THE CONSISTENCY OF YOUR DISK
       AND LOAD ANY DATA STRUCTURES INTO MEMORY */

    //read vcb
	dnode root;
	dirent root_dirent;

    dread(0, (char *)&head);

    if(head.magic != 0x77) {
        //throw an error
        perror("Magic is incorrect.\n");
        dunconnect();
        return NULL;
    }

	if(head.dirty) {
		perror("Filesystem wasn't unmounted correctly");
	}

	head.dirty = 1;
	dwrite(0, (char *)&head);

	dread(head.root.block, (char *)&root);

	//Do something with root?

	dread(2, (char *)&root_dirent);

	//root dirent should have entries for "." and ".." pointing to the root dnode
	if(strcmp(root_dirent.entries[0].name,".") != 0 || root_dirent.entries[0].block.block != head.root.block) {
		printf("first entry should have \".\" and point to root dnode.\n");
		return NULL;
	}
	if(strcmp(root_dirent.entries[1].name,"..") != 0 || root_dirent.entries[1].block.block != head.root.block) {
		printf("first entry should have \"..\" and point to root dnode.\n");
		return NULL;
	}


    /* initialize cache */
    c = calloc(1, sizeof(cache));
    for (int i = 0; i < 3; i++) {
        c->entries[i].blocknum = i;
    }

    memcpy(c->entries[0].data, (char*)&head, BLOCKSIZE);
    memcpy(c->entries[1].data, (char*)&root, BLOCKSIZE);
    memcpy(c->entries[2].data, (char*)&root_dirent, BLOCKSIZE);

    return NULL;
}

/*
 * Called when your file system is unmounted.
 *
 */
static void vfs_unmount (void *private_data) {
  fprintf(stderr, "vfs_unmount called\n");

  /* 3600: YOU SHOULD ADD CODE HERE TO MAKE SURE YOUR ON-DISK STRUCTURES
           ARE IN-SYNC BEFORE THE DISK IS UNMOUNTED (ONLY NECESSARY IF YOU
           KEEP DATA CACHED THAT'S NOT ON DISK */

	//Add metadata about write state, a "clean" tag

    /* write cache to disk */
	for (int i = 0; i < 3; i++) {
        dwrite(i, c->entries[i].data);
    }

    free(c);

	head.dirty = 0;
	dwrite(0, (char *)&head);

  // Do not touch or move this code; unconnects the disk
  dunconnect();
}

/*
 *
 * Given an absolute path to a file/directory (i.e., /foo ---all
 * paths will start with the root directory of the CS3600 file
 * system, "/"), you need to return the file attributes that is
 * similar stat system call.
 *
 * HINT: You must implement stbuf->stmode, stbuf->st_size, and
 * stbuf->st_blocks correctly.
 *
 */
static int vfs_getattr(const char *path, struct stat *stbuf) {
	//until multidirectory is implemented, our_node is always the root dnode
	dnode our_node;
	cdread(1, (char *)&our_node);
	//direntry our_direntry;
	direntry tmp;

	if(!strcmp(path, "/") == 0) {
		if(path_exists(path, &tmp)) {
			cdread(tmp.block.block, (char *) &our_node);
		} else {
			return -ENOENT;
		}	
	}

  // Do not mess with this code 
  stbuf->st_nlink = 1; // hard links
  stbuf->st_rdev  = 0;
  stbuf->st_blksize = BLOCKSIZE;

  /* 3600: YOU MUST UNCOMMENT BELOW AND IMPLEMENT THIS CORRECTLY */
  
  
  	if(strcmp(path,"/") == 0) {
    	stbuf->st_mode  = 0777 | S_IFDIR;
  	} else if (tmp.type == DIRENTRY_DIR) {
		stbuf->st_mode = our_node.mode | S_IFDIR;
	} else {
   		 stbuf->st_mode  = our_node.mode | S_IFREG;
	}
  stbuf->st_uid     = our_node.user; // file uid
  stbuf->st_gid     = our_node.group; // file gid
  stbuf->st_atime   = our_node.access_time.tv_sec; // access time 
  stbuf->st_mtime   = our_node.modify_time.tv_sec; // modify time
  stbuf->st_ctime   = our_node.create_time.tv_sec;// create time

	//What is the proper handling for stat of a directory?
	//Temporarily just check if the path is the root 
	if(strcmp(path, "/") == 0) {
		stbuf->st_size    = BLOCKSIZE; // file size
		stbuf->st_blocks  = 1; // file size in blocks
	} else {
		stbuf->st_size    = our_node.size; // file size
		stbuf->st_blocks  = our_node.size / BLOCKSIZE + 1; // file size in blocks
	}

  return 0;
}

/*
 * Given an absolute path to a directory (which may or may not end in
 * '/'), vfs_mkdir will create a new directory named dirname in that
 * directory, and will create it with the specified initial mode.
 *
 * HINT: Don't forget to create . and .. while creating a
 * directory.
 */
/*
 * NOTE: YOU CAN IGNORE THIS METHOD, UNLESS YOU ARE COMPLETING THE 
 *       EXTRA CREDIT PORTION OF THE PROJECT.  IF SO, YOU SHOULD
 *       UN-COMMENT THIS METHOD.
 */

static int vfs_mkdir(const char *path, mode_t mode) {
	if(path_exists(path, NULL)) {
		return -EEXIST;
	}

	//Check permissions
	if(0) {
		return -EACCES;
	}
	
	//Create and write the new directory
	dnode node;
	init_dnode(&node);
	node.mode = mode;

	blocknum dnode_block = head.free;
	vcb_update_free();

	cdwrite(dnode_block.block, (char *)&node);

	//Initialize the first dirent for the new dnode, write to disk
	dirent first_dirent;
	init_dirent(&first_dirent, dnode_block);
	cdwrite(node.direct[0].block, (char *)&first_dirent);

	//strdup creates a malloc'd buffer
	char * name = strdup(path);
	if(name[strlen(name) -1] == '/') {
		name[strlen(name) -1] = '\0';
	}
	char * filename = file_name(name);
	char * dir = dir_name(name);
	
	//free the malloc
	free(name);
	
	//Create direntry to contain the new dir
	direntry new_direntry;
	strncpy(new_direntry.name, filename, sizeof(new_direntry.name));
	
	free(filename);

	new_direntry.type = DIRENTRY_DIR;
	new_direntry.block = dnode_block;

	//Find the containing directory, and add our new direntry to it
	dnode contain_dir;
	if(strcmp("/", dir) == 0) {
		cdread(1, (char *)&contain_dir);	
		add_direntry(&contain_dir, 1, new_direntry);
	} else {
		direntry tmp;
		path_exists(dir, &tmp);
		cdread(tmp.block.block, (char *)&contain_dir);
		add_direntry(&contain_dir, tmp.block.block, new_direntry);
	}
	free(dir);

	return 0;
} 

/** Read directory
 *
 * Given an absolute path to a directory, vfs_readdir will return 
 * all the files and directories in that directory.
 *
 * HINT:
 * Use the filler parameter to fill in, look at fusexmp.c to see an example
 * Prototype below
 *
 * Function to add an entry in a readdir() operation
 *
 * @param buf the buffer passed to the readdir() operation
 * @param name the file name of the directory entry
 * @param stat file attributes, can be NULL
 * @param off offset of the next entry or zero
 * @return 1 if buffer is full, zero otherwise
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *                                 const struct stat *stbuf, off_t off);
 *			   
 * Your solution should not need to touch fi
 *
 */
static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
	dnode dir;
	//Check for existance of the path, and if it exists read the dnode
	if(strcmp(path, "/") == 0) {
		cdread(1, (char *)&dir);
	} else {
		direntry tmp;
		if(!path_exists(path, &tmp)) {
			return -ENOENT;
		}
		cdread(tmp.block.block, (char *)&dir);
	}

	char * file;
	unsigned outer_offset = offset / ENTRIES_IN_DIR;
	unsigned inner_offset = offset % ENTRIES_IN_DIR;
	unsigned count = offset;

	//Iterate over each block in the directory, and get its file contents
	for(; outer_offset < MAX_BLOCKS; ++outer_offset) {
		if(count >= dir.size) { return 0; }
		blocknum block = get_node_block(&dir, outer_offset);
		if(!block.valid) {
			continue;
		}
		
		dirent de;
		cdread(block.block, (char *)&de);
		
		for(; inner_offset < ENTRIES_IN_DIR; ++inner_offset) {
			if(count >= dir.size) {
				return 0;
			}
	
			if(!de.entries[inner_offset].block.valid) {
				continue;
			}
			file = de.entries[inner_offset].name;	
			unsigned next = outer_offset * ENTRIES_IN_DIR + inner_offset + 1;
			if(filler(buf, file, NULL, next)) {
				return 0;
			}
			++count;
		}
		inner_offset = 0;
	}

	return 0;
}
/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	//No space left in the filesystem, cannot create a new file
	if(!head.free.valid) {
		return -ENOSPC;
	}  
	
	if(path_exists(path, NULL)) {
		return -EEXIST;
	}
	
	/*
	char * name = path + 1;
	//Feature add: for multidirectory, create directories as needed so that path can be made	
	if(file_exists(&root, name, NULL)) {
		return -EEXIST;
	}*/
	

	//create inode for file
	inode our_inode;

	memset(&our_inode, 0, BLOCKSIZE);

	our_inode.size = 0;
	our_inode.user = geteuid();
	our_inode.group = getegid();
	our_inode.mode = mode;
	
	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);

	our_inode.access_time = time;
	our_inode.create_time = time;
	our_inode.modify_time = time;

	//direct, indirect, double indirect have no data at the start

	//get a block for the inode from the vcb free list
	blocknum block_inode = head.free;
	vcb_update_free();
	cdwrite(block_inode.block, (char *)&our_inode);


	//create direntry for inode
	direntry our_direntry;
	//direntry has field char * name[59], therefore cannot have a name of longer
	//than 59 characters \0 included
	
	char * name = file_name(path);
	char * dir = dir_name(path);

	strncpy(our_direntry.name, name, 59);

	free(name);
	

	our_direntry.type = DIRENTRY_FILE;	
	our_direntry.block = block_inode;	
	
	
	//Find the containing directory
	dnode our_node;
	unsigned write_block;
	if(strcmp(dir, "/") == 0) {
		cdread(1, (char *)&our_node);
		write_block = 1;
	} else {
		direntry tmp;
		if(!path_exists(dir, &tmp)) {
			vfs_mkdir(dir, mode);
			if(!path_exists(dir, &tmp)) {
				return -1;
			}
		}
		cdread(tmp.block.block, (char *)&our_node);
		write_block = tmp.block.block;
	}
	

	//add direntry to dir's direct/indirect/double indirect blocks
    // 1 is block of root dnode
	//add_direntry(&root, 1, our_direntry);

	add_direntry(&our_node, write_block, our_direntry);
	free(dir);

	return 0;
}

/*
 * The function vfs_read provides the ability to read data from 
 * an absolute path 'path,' which should specify an existing file.
 * It will attempt to read 'size' bytes starting at the specified
 * offset (offset) from the specified file (path)
 * on your filesystem into the memory address 'buf'. The return 
 * value is the amount of bytes actually read; if the file is 
 * smaller than size, vfs_read will simply return the most amount
 * of bytes it could read. 
 *
 * HINT: You should be able to ignore 'fi'
 *
 */
static int vfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
	unsigned int bytes_read = 0;
	//Check for existance of the path
	direntry tmp;
	if(!path_exists(path, &tmp)) {
		return 0;
	}
	
	//Initialize the node pointed to by path
	inode node;
	cdread(tmp.block.block, (char *)&node);

	char data[BLOCKSIZE];
	blocknum block;
	unsigned int startblock = offset / BLOCKSIZE;

	//If the offset isn't a multiple of the blocksize, we need to get only a portion of the 
	//data from one of the blocks
	if(offset % BLOCKSIZE != 0) {
		block = get_node_block(&node, startblock);
		if(!block.valid) {
			return 0;
		}
		cdread(block.block, data);
        unsigned int internal_offset = offset % BLOCKSIZE;
        /* everything else in the block, which will be (over)written */
        unsigned int remainder = BLOCKSIZE - internal_offset;
        memcpy(buf, data + internal_offset, remainder);
		bytes_read += offset % BLOCKSIZE; 
		++startblock;
	}	

	//So long as there are sill bytes to read, read them
	while(bytes_read < size) {
		block = get_node_block(&node, startblock);

		//If we reach the end of the file, stop
		if(!block.valid) { 
			break; 
		}
		cdread(block.block, data);

		if(size - bytes_read < BLOCKSIZE) {
			memcpy(buf + bytes_read, data, size - bytes_read);
			bytes_read = size;
		} else {
			memcpy(buf + bytes_read, data, BLOCKSIZE);
			bytes_read += BLOCKSIZE;
		} 
		++startblock;
	}

    return bytes_read;
}

/*
 * The function vfs_write will attempt to write 'size' bytes from 
 * memory address 'buf' into a file specified by an absolute 'path'.
 * It should do so starting at the specified offset 'offset'.  If
 * offset is beyond the current size of the file, you should pad the
 * file with 0s until you reach the appropriate length.
 *
 * You should return the number of bytes written.
 *
 * HINT: Ignore 'fi'
 */
static int vfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{

  /* 3600: NOTE THAT IF THE OFFSET+SIZE GOES OFF THE END OF THE FILE, YOU
           MAY HAVE TO EXTEND THE FILE (ALLOCATE MORE BLOCKS TO IT). */
	unsigned int bytes_written = 0;

	direntry inode_entry;
	
	//Can't find the file specified
	if(!path_exists(path, &inode_entry)) {
		printf("File does not exist!");
		return 0;
	}

	inode node;
	cdread(inode_entry.block.block, (char *)&node);

	int writeblock = offset / BLOCKSIZE;
	char data[BLOCKSIZE];

    /* if offset isn't aligned on a block */
	if(offset % BLOCKSIZE != 0) {
		blocknum block = get_node_block(&node, writeblock);

		if(!block.valid) {
			memset(data, 0, BLOCKSIZE);
		} else {
			cdread(block.block, data);
		}

        /* bytes at the beginning of the block that shouldn't change */
        unsigned int internal_offset = offset % BLOCKSIZE;
        /* everything else in the block, which will be (over)written */
        unsigned int remainder = BLOCKSIZE - internal_offset;
        memcpy(data + internal_offset, buf, remainder);

		write_node_block(&node, inode_entry.block, data, writeblock);
		writeblock++;
		bytes_written = remainder;
	}

    /* while we still have data to write */
	while(bytes_written < size) {
		if(size - bytes_written < BLOCKSIZE) {
        /* if we have less than a block left */
			memset(data, 0, BLOCKSIZE);
			memcpy(data, buf + bytes_written, size - bytes_written);
			write_node_block(&node, inode_entry.block, data, writeblock);
            bytes_written += size - bytes_written;
            assert(bytes_written == size);
		} else {
        /* if we have more than a block left */
			write_node_block(&node, inode_entry.block, buf + bytes_written, writeblock);
			bytes_written += BLOCKSIZE;
		}
		writeblock++;
	}

	if(offset + size > node.size) {
		node.size = offset + size;
		cdwrite(inode_entry.block.block, (char *)&node);
	}	

  return bytes_written;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{

  /* 3600: NOTE THAT THE BLOCKS CORRESPONDING TO THE FILE SHOULD BE MARKED
           AS FREE, AND YOU SHOULD MAKE THEM AVAILABLE TO BE USED WITH OTHER FILES */

	//Cannot delete the root directory
	if(strcmp(path, "/") == 0) {
		return -EACCES;
	}
	
	//Check for existance of the path
	direntry de;
	if(!path_exists(path, &de)) {
		printf("File not found\n");
		return -ENOENT;
	}

	//If the path requested is a file, remove it
	if(de.type == DIRENTRY_FILE) {
		inode node;
		cdread(de.block.block, (char *)&node);
		//Remove all blocks that the file contains
		for(unsigned offset = 0; offset < MAX_BLOCKS; ++offset) {
			blocknum block = get_node_block(&node, offset);
			if(!block.valid) {
				break;
			}	
			release_block(block);
		}
		
		char * tmp = strdup(path);
		if(tmp[strlen(tmp) - 1] == '/') {
			tmp[strlen(tmp) -1] = '\0';
		}
		char * dir = dir_name(tmp);
		char * filename = file_name(tmp);
		free(tmp);		

		direntry contain_de;
		if(!path_exists(dir, &contain_de)) {
			//How did you get here?
			return 0;
		}
		free(dir);
		//Find the directory containing our file
		dnode containing_dir;
		cdread(contain_de.block.block, (char *)&containing_dir);
		for(unsigned offset = 0; offset < MAX_BLOCKS; ++offset) {
			blocknum block = get_node_block(&containing_dir, offset);
			if(!block.valid) {
				continue;
			}
			dirent d;
			cdread(block.block, (char *)&d);
			for(int i = 0; i < ENTRIES_IN_DIR; ++i) {
				//And if we find it, remove it
				if(strcmp(d.entries[i].name, filename) == 0) {
					free(filename);
							
					release_block(de.block);
	
					direntry empty;
                	memset(&empty, 0, sizeof(empty));
                	d.entries[i] = empty;
					
					cdwrite(block.block, (char *)&d);
					containing_dir.size--;
					cdwrite(contain_de.block.block, (char *)&containing_dir);
					return 0;
				}
			}
		}

		return 0;
	//Delete cannot remove directories
	} else {
		return -EISDIR;
	}
}
/*
 * The function rename will rename a file or directory named by the
 * string 'oldpath' and rename it to the file name specified by 'newpath'.
 *
 * HINT: Renaming could also be moving in disguise
 *
 */
static int vfs_rename(const char *from, const char *to)
{
	//as to not conflict with root global
	dnode root;
	cdread(1, (char *)&root);

	direntry from_file;

	if(!path_exists(from, &from_file)) {
		return 0;
	}

	direntry to_file;
	if(path_exists(to, &to_file)) {
		vfs_delete(to);
	}


    return 0;
}


/*
 * This function will change the permissions on the file
 * to be mode.  This should only update the file's mode.  
 * Only the permission bits of mode should be examined 
 * (basically, the last 16 bits).  You should do something like
 * 
 * fcb->mode = (mode & 0x0000ffff);
 *
 */
static int vfs_chmod(const char *file, mode_t mode)
{
	direntry inode_entry;
	if(!path_exists(file, &inode_entry)) {
		return 0;
	}
	inode node;
	cdread(inode_entry.block.block, (char *)&node);

	node.mode = mode & 0xffff;

	cdwrite(inode_entry.block.block, (char *)&node);

    return 0;
}

/*
 * This function will change the user and group of the file
 * to be uid and gid.  This should only update the file's owner
 * and group.
 */
static int vfs_chown(const char *file, uid_t uid, gid_t gid)
{
	direntry inode_entry;
	if(!path_exists(file, &inode_entry)) {
		return 0;
	}
	inode node;
	cdread(inode_entry.block.block, (char *)&node);

	node.user = uid;
	node.group = gid;

	cdwrite(inode_entry.block.block, (char *)&node);

    return 0;
}

/*
 * This function will update the file's last accessed time to
 * be ts[0] and will update the file's last modified time to be ts[1].
 */
static int vfs_utimens(const char *file, const struct timespec ts[2])
{
	direntry inode_entry;
	if(!path_exists(file, &inode_entry)) {
		return 0;
	}
	inode node;
	cdread(inode_entry.block.block, (char *)&node);

	node.access_time = ts[0];
	node.modify_time = ts[1];

	cdwrite(inode_entry.block.block, (char *)&node);

    return 0;
}

/*
 * This function will truncate the file at the given offset
 * (essentially, it should shorten the file to only be offset
 * bytes long).
 */
static int vfs_truncate(const char *file, off_t offset)
{

  /* 3600: NOTE THAT ANY BLOCKS FREED BY THIS OPERATION SHOULD
           BE AVAILABLE FOR OTHER FILES TO USE. */

	direntry inode_entry;
	if(!path_exists(file, &inode_entry)) {
		return 0;
	}
	
	inode node;
	cdread(inode_entry.block.block, (char *)&node);

	if(offset > node.size) { return 0; }
	int block_to_remove = 0;
	if(offset != 0) {
		block_to_remove = (offset)/ BLOCKSIZE + 1;
	}
	blocknum nullblock = (blocknum){0, 0};

	for(; block_to_remove <= MAX_BLOCKS; ++block_to_remove){
		blocknum block = get_node_block(&node, block_to_remove);
		if(!block.valid) { break; }
		set_node_block(&node, inode_entry.block, nullblock, block_to_remove);
		release_block(block);
	}

	if(offset < node.size) {
		node.size = offset;
		cdwrite(inode_entry.block.block, (char *)&node);
	}

    return 0;
}


/*
 * You shouldn't mess with this; it sets up FUSE
 *
 * NOTE: If you're supporting multiple directories for extra credit,
 * you should add 
 *
 *     .mkdir	 = vfs_mkdir,
 */
static struct fuse_operations vfs_oper = {
    .init    = vfs_mount,
    .destroy = vfs_unmount,
    .getattr = vfs_getattr,
    .readdir = vfs_readdir,
    .mkdir	 = vfs_mkdir,
	.create	 = vfs_create,
    .read	 = vfs_read,
    .write	 = vfs_write,
    .unlink	 = vfs_delete,
    .rename	 = vfs_rename,
    .chmod	 = vfs_chmod,
    .chown	 = vfs_chown,
    .utimens	 = vfs_utimens,
    .truncate	 = vfs_truncate,
};

int main(int argc, char *argv[]) {
    /* Do not modify this function */
    umask(0);
    if ((argc < 4) || (strcmp("-s", argv[1])) || (strcmp("-d", argv[2]))) {
      printf("Usage: ./3600fs -s -d <dir>\n");
      exit(-1);
    }
    return fuse_main(argc, argv, &vfs_oper, NULL);
}

