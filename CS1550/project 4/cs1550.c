/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nIndexBlock;				//where the index block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int) - sizeof(long)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	long lastAllocatedBlock; //The number of the last allocated block
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int) - sizeof(long)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How many entries can one index block hold?
#define	MAX_ENTRIES_IN_INDEX_BLOCK (BLOCK_SIZE/sizeof(long))

struct cs1550_index_block
{
      //All the space in the index block can be used for index entries.
			// Each index entry is a data block number.
      long entries[MAX_ENTRIES_IN_INDEX_BLOCK];
};

typedef struct cs1550_index_block cs1550_index_block;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

struct cs1550_disk_block
{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

//define helper functions
void parse_path(const char *path, char *directory, char *filename, char *extension);
cs1550_root_directory open_root(void);
cs1550_directory_entry open_dir(long blockNum);
cs1550_index_block open_file(long blockNum);
long useNextFreeBlock(void);
void write_root(cs1550_root_directory *root);
void write_directory_entry(cs1550_directory_entry *dir, long blockNum);
void write_index_block(cs1550_index_block *index, long blockNum);
void write_disk_block(cs1550_disk_block *disk_block, long blockNum);
long check_subdir(char *directory);
long check_file(char *directory, char *filename, char *extension);

/* Thanks to Mohammad Hasanzadeh Mofrad (@moh18) for these
   two functions */
static void * cs1550_init(struct fuse_conn_info* fi)
{
	  (void) fi;
    printf("We're all gonna live from here ....\n");
    cs1550_root_directory *root = (cs1550_root_directory *) malloc(sizeof(cs1550_root_directory));
    root->lastAllocatedBlock = (long) 0;
    root->nDirectories = 0;
		printf("gonna write root\n");
    write_root(root);
		free(root);
		printf("finish init");
		return NULL;
}

static void cs1550_destroy(void* args)
{
		(void) args;
    printf("... and die like a boss here\n");
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 *
 * Return 0 on success, with correctly set structure
 * Return -ENOENT if file not found
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));

	//is path the root dir ?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
    char directory[MAX_FILENAME + 1]; //all strings need space for null terminator
    char filename[MAX_FILENAME + 1];
    char extension[MAX_EXTENSION + 1];
    parse_path(path, directory, filename, extension);

    if (strncmp(directory, "\0", 1) != 0 && strncmp(filename, "\0", 1) == 0) { //Check if name is subdirectory
      if (check_subdir(directory) != -1) {
        //Might want to return a structure with these fields
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        res = 0; //no error
      } else {
        res = -ENOENT;
      }
    } else if (strncmp(directory, "\0", 1) != 0 && strncmp(filename, "\0", 1) != 0) { //Check if name is a regular file
      //regular file, probably want to be read and write
      stbuf->st_mode = S_IFREG | 0666;
      stbuf->st_nlink = 1; //file links
      stbuf->st_size = 0; //file size - make sure you replace with real size!
      res = 0; // no error
	  } else {
      //Else return that path doesn't exist
      res = -ENOENT;
    }
  }
	return res;
}

/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 *
 * Return 0 on success
 * Return -ENOENT if dir is not valid or not found
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0) { //if root dir
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    cs1550_root_directory root = open_root();
		int i;
    for (i=0; i < root.nDirectories; i++) {
      filler(buf, root.directories[i].dname + 1, NULL, 0);
    }
  } else { //subdirectory
    char directory[MAX_FILENAME + 1]; //all strings need space for null terminator
    char filename[MAX_FILENAME + 1];
    char extension[MAX_EXTENSION + 1];
    parse_path(path, directory, filename, extension);

    if (strncmp(directory, "\0", 1) != 0 && strncmp(filename, "\0", 1) == 0) { //if is valid subdir
      int nStartBlock = check_subdir(directory);
      if (nStartBlock == -1) { //subdir not found
        return -ENOENT;
      }
      //else directory is in disk
      filler(buf, ".", NULL, 0);
      filler(buf, "..", NULL, 0);

			cs1550_directory_entry dir = open_dir(nStartBlock);
			int i;
			for(i=0; i < dir.nFiles; i++) {
				char name[MAX_FILENAME + MAX_EXTENSION + 2]; //space for null term + .
				strcpy(name, dir.files[i].fname);
				if (strncmp(dir.files[i].fext, "\0", 1) != 0) {
					strcat(name, ".");
					strcat(name, dir.files[i].fext);
				}
				filler(buf, name + 1, NULL, 0);
			}
    }
  }

	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 *
 * Return 0 on success
 * Return -ENAMETOOLONG if name > 8 chars
 * Return -EPERM if directory isn't under root dir only
 * Return -EEXIST if directory already exists
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;

  char directory[MAX_FILENAME]; //all strings need space for null terminator
  char filename[MAX_FILENAME + 1];
  char extension[MAX_EXTENSION + 1];
  parse_path(path, directory, filename, extension);

  if (strlen(directory) > MAX_FILENAME) { //file name too long
    return -ENAMETOOLONG;
  }

  if (strncmp(filename, "\0", 1) != 0) { //if filename not empty string => trying to make subdir not in root dir
    return -EPERM;
  }

  long nStartBlock = check_subdir(directory);
  if (nStartBlock != -1) {
    //directory already exists
    return -EEXIST;
  }

  cs1550_root_directory root = open_root();
  if (root.nDirectories < MAX_DIRS_IN_ROOT) { //if able to create more
    long freeBlock = root.lastAllocatedBlock + 1; //block num of new directory entry

    //update root directory
    strncpy(root.directories[root.nDirectories].dname, directory, MAX_FILENAME); //add subdir name to root
    root.directories[root.nDirectories].nStartBlock = freeBlock;
    root.nDirectories++;
    root.lastAllocatedBlock++;
    write_root(&root);

    //create directory entry
    cs1550_directory_entry dir;
		memset(&dir, 0, BLOCK_SIZE); //clear mem just in case
    dir.nFiles = 0; //starts empty

    //write directory entry to free block
    FILE *disk = fopen(".disk", "r+b");
    fseek(disk, freeBlock * BLOCK_SIZE, SEEK_SET);
    fwrite(&dir, BLOCK_SIZE, 1, disk); //write directory entry block
    fclose(disk);

  } else {
    printf("Cannot create any more files here");
    //throw error???
  }
	return 0;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 * Return 0 on success
 * Return -ENAMETOOLONG if name beyond 8 chars ?
 * Return -EPERM if file is trying to be created in root dir
 * Return -EEXIST if file already exists
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	(void) path;

  char directory[MAX_FILENAME + 1]; //all strings need space for null terminator
  char filename[MAX_FILENAME];
  char extension[MAX_EXTENSION];
  parse_path(path, directory, filename, extension);

  if (strlen(directory) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) { //filename/ext too long
    return -ENAMETOOLONG;
  }

  if (strncmp(filename, "\0", 1) == 0) { //if filename is empty string => trying to make file in root dir
    return -EPERM;
  }

  long nStartBlock = check_subdir(directory);
  if (nStartBlock != -1) { //if directory exists
    //read in the corresponding cs1550_directory_entry
    FILE *disk = fopen(".disk", "r+b");
    fseek(disk, nStartBlock * BLOCK_SIZE, SEEK_SET);
    cs1550_directory_entry dir;
    fread(&dir, BLOCK_SIZE, 1, disk);
    fclose(disk);

    //look for matching files in directory
    int i;
    for(i=0; i < dir.nFiles; i++) {
      struct cs1550_file_directory currFileDir = dir.files[i];
      if (strncmp(currFileDir.fname, filename, MAX_FILENAME) == 0 && strncmp(currFileDir.fext, extension, MAX_EXTENSION) == 0) {
        //file already in directory
        return -EEXIST;
      }
    }
		long indexBlockLoc = useNextFreeBlock();

		//update directory entry
    struct cs1550_file_directory new_file_dir;
    strncpy(new_file_dir.fname, filename, MAX_FILENAME);
    strncpy(new_file_dir.fext, extension, MAX_EXTENSION);
		new_file_dir.fsize = 0;
		new_file_dir.nIndexBlock = indexBlockLoc;
    dir.files[dir.nFiles] = new_file_dir;
		dir.nFiles++;
		write_directory_entry(&dir, nStartBlock);

		//create and write index block
		struct cs1550_index_block indexBlock;
		write_index_block(&indexBlock, indexBlockLoc);

  } else {
    //throw error?
		printf("cannot find directory");
  }

	return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	size = 0;

	return size;
}

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	return size;
}

//HELPER FUNCTIONS =============================================================
// parse the path name into directory, filename, and extension
void parse_path(const char *path, char *directory, char *filename, char *extension) {
  //make sure each ends with null terminator
  directory[0] = '\0';
  filename[0] = '\0';
  extension[0] = '\0';
  sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension); //read into variables
}

//open disk, return root directory
cs1550_root_directory open_root(void) {
  cs1550_root_directory root;

  FILE *f = fopen(".disk", "rb");
  fseek(f, 0, SEEK_SET); //root at block 0
  fread(&root, BLOCK_SIZE, 1, f);
  return root;
}

//open disk, return cs1550_directory_entry at given block number
cs1550_directory_entry open_dir(long blockNum) {
  cs1550_directory_entry dir;

  FILE *f = fopen(".disk", "rb");
  fseek(f, blockNum * BLOCK_SIZE, SEEK_SET); //seek to location
  fread(&dir, BLOCK_SIZE, 1, f);
  return dir;
}

//open disk, return cs1550_index_block at given block number
cs1550_index_block open_file(long blockNum) {
  cs1550_index_block index;

  FILE *f = fopen(".disk", "rb");
  fseek(f, blockNum * BLOCK_SIZE, SEEK_SET); //seek to location
  fread(&index, BLOCK_SIZE, 1, f);
  return index;
}

// return next free block, then increment lastAllocatedBlock
long useNextFreeBlock() {
  cs1550_root_directory root = open_root();
  long ret = root.lastAllocatedBlock + 1;
	root.lastAllocatedBlock++;
  write_root(&root);
  return ret;
}

//write/overwrite root block
void write_root(cs1550_root_directory *root) {
	printf("write started\n");
  FILE *disk = fopen(".disk", "r+b");
  fwrite(root, BLOCK_SIZE, 1, disk);
  fclose(disk);
	printf("write ended\n");
}

//write/overwrite directory entry into given block number
void write_directory_entry(cs1550_directory_entry *dir, long blockNum) {
  FILE *disk = fopen(".disk", "r+b");
  fseek(disk, blockNum * BLOCK_SIZE, SEEK_SET);
  fwrite(dir, BLOCK_SIZE, 1, disk);
  fclose(disk);
}

//write/overwrite index block into given block number
void write_index_block(cs1550_index_block *index, long blockNum) {
  FILE *disk = fopen(".disk", "r+b");
  fseek(disk, blockNum * BLOCK_SIZE, SEEK_SET);
  fwrite(index, BLOCK_SIZE, 1, disk);
  fclose(disk);
}

//write/overwrite index block into given block number
void write_disk_block(cs1550_disk_block *disk_block, long blockNum) {
  FILE *disk = fopen(".disk", "r+b");
  fseek(disk, blockNum * BLOCK_SIZE, SEEK_SET);
  fwrite(disk_block, BLOCK_SIZE, 1, disk);
  fclose(disk);
}

// if subdir exists, return its start block. otherwise return -1
long check_subdir(char *directory) {
  cs1550_root_directory root;
	root = open_root();
	int i;
  for (i=0; i < root.nDirectories; i++) {
    char testDir[MAX_FILENAME + 1];
		strncpy(testDir, root.directories[i].dname, MAX_FILENAME);
    if (strncmp(directory, testDir, MAX_FILENAME) == 0) { //check name against directory name in root
      return root.directories[i].nStartBlock; //return start block
    }
  }
  return -1;
}

// if file exists, return its start block. otherwise return -1
long check_file(char *directory, char *filename, char *extension) {
  cs1550_root_directory root = open_root();
	int i;
  for (i=0; i < root.nDirectories; i++) {
		char testDir[MAX_FILENAME + 1];
		strncpy(testDir, root.directories[i].dname, MAX_FILENAME);
    if (strncmp(directory, testDir, MAX_FILENAME) == 0) { //check name against directory name in root
      long dirStartBlock = root.directories[i].nStartBlock; //return start block
			cs1550_directory_entry dir = open_dir(dirStartBlock);

			int j;
			for (j=0; j < dir.nFiles; j++) {
					if (strcmp(dir.files[j].fname, filename) == 0 && strcmp(dir.files[j].fext, extension) == 0) {
						//match filename and extension!
						return dir.files[j].nIndexBlock;
					}
			}
    }
  }
  return -1;
}

//DO NOT MODIFY BELOW THIS LINE ================================================

/*
 * Removes a directory.
 */
// DO NOT MODIFY
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/*
 * Deletes a file
 */
// DO NOT MODIFY
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
// DO NOT MODIFY
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
// DO NOT MODIFY
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
// DO NOT MODIFY
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}

//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
		.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
		.mknod	= cs1550_mknod,
		.unlink = cs1550_unlink,
		.truncate = cs1550_truncate,
		.flush = cs1550_flush,
		.open	= cs1550_open,
		.init = cs1550_init,
    .destroy = cs1550_destroy,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
