#define _OSH_FS_VERSION 2018051000
#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdio.h>


struct filenode {
	char filename[256];
	struct data_lnode *content;
	struct data_lnode *tail;
	struct stat *st;
	struct filenode *next;
};

struct data_lnode {
	int i;//ith block in mem
	struct data_lnode* next;
};

#define blocknr 16*1024
#define blocksize 128*1024//a 2G FILESYSTEM,MAX FILE SIZE 1000M

#define lnodesize sizeof(data_lnode)

//static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[blocknr];
int last_applied=0;
static struct filenode *root = NULL;
char used[blocknr]={0};

int find_vacancy() {
	int i, j;
	for (i = last_applied, j = 0; used[i] != 0 && j < blocknr; i++, j++);
	if (j == blocknr) {
		perror("no vacancy");
		return -1;
	}
	else {
		last_applied = i;
		return i;
	}
}//find_vacancy

int find_address(void * p) {
	for (int i = 0; i < blocknr; i++) {
		if (used[i] == 1 && p == mem[i]) return i;
	}
	return -1;
}
void mark_used(int i) {
	used[i] = 1;
	return;
}
void mark_unused(int i) {
	used[i] = 0;
	return;
}
void *dlmalloc() {

	int i = find_vacancy();
	mem[i]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(mem[i], 0, blocksize);
	mark_used(i);
	return mem[i];
}

static void create_filenode(const char *filename, const struct stat *st) {
	struct filenode *newnode = (struct filenode *)dlmalloc();//我们有理由认为filenode小于一个block大小,have calculated
	memcpy(newnode->filename, filename, strlen(filename) + 1);
	puts(newnode->filename);
	newnode->st=(struct stat*)dlmalloc();
	memcpy(newnode->st, st, sizeof(struct stat));
	newnode->next = root;
	newnode->content = (struct data_lnode*)dlmalloc();
	int i;
	i=find_vacancy();
	mem[i]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mark_used(i);
	newnode->content->i=i;
	newnode->content->next = NULL;
	newnode->tail = newnode->content;
	root = newnode;
}

static struct filenode *get_filenode(const char *name)
{
	struct filenode *node = root;
	while (node) {
		if (strcmp(node->filename, name + 1) != 0)
			node = node->next;
		else
			return node;
	}

	return NULL;
}


static struct filenode *get_filenode_last(const char *name)
{/*search for the previous node of filenode "name"*/
    struct filenode *node = root;
    struct filenode *node_last=NULL;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0){
	    node_last=node;
            node = node->next;
	}
        else
            return node_last;
    }
    return node_last;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    /*size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    size_t blocksize = size / blocknr;
    // Demo 1
    for(int i = 0; i < blocknr; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    // Demo 2
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < blocknr; i++) {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }*/
    memset(used,0,blocknr*sizeof(char));
    return NULL;
}


static int oshfs_getattr(const char *path, struct stat *stbuf)
{
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if (strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
	}
	else if (node) {
		memcpy(stbuf, node->st, sizeof(struct stat));
	}
	else {
		ret = -ENOENT;
	}
	return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct filenode *node = root;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while (node) {
		filler(buf, node->filename, node->st, 0);
		node = node->next;
	}
	return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	struct stat st;
	st.st_mode = S_IFREG | 0644;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = 0;
	create_filenode(path + 1, &st);
	return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	//adopt "file address linklist" to record the number of every block.
	int a_length;
	struct filenode *node = get_filenode(path);
	if(node==NULL) return 0;
	int o_length =((int)(node->st->st_size)-1)/(int)(blocksize) + 1;//ceiling

	if (offset + size > node->st->st_size) {
		//notice! we require >,else remain the same size
		node->st->st_size = offset + size;
		if(offset +size > o_length*(int)blocksize){
			//there is a detail:if st_size<offset+size<o_length*blocksize,then another block IS NOT needed.
			a_length = (int)(offset + size - o_length*(int)blocksize-1)/(int)(blocksize)+1;//现在需要加上（additional)a_length块
			//add a_length blocks in the original lnodelist
			for (int i = 0; i < a_length; i++) {
				node->tail->next = node->tail + 1;//+1 in the basis of lnodesize
				node->tail = node->tail->next;
				node->tail->i = find_vacancy();//note:there IS NOT a pointer pointing to the mem[i]space.we use i to locate the space
				mem[node->tail->i]= mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				mark_used(node->tail->i);//notice:for efficiency's sake,we'd better modify the dlmallloc() later;
				node->tail->next = NULL;
			}
		}
	}

	//文件数据链表构建完毕，开始写入数据
	char *mbuf;
	size_t msize;
	mbuf=buf;
	msize=size;
	int remnant, remvac;
	remnant = offset-(int)(offset)/(int)(blocksize)*(int)blocksize;//similar to remainder.remnant refers to the last part of data, which cannot fullfil the last datablock
	remvac = blocksize - remnant;//remvac means the remained available vacancy in the last block
	int start = ((int)(offset)-1) / (int)(blocksize)+1;//start : the number of first write target
	struct data_lnode *cur;
	cur = node->content;
	if(msize>remvac){
		for (int i = 0; i < start-1; i++) cur = cur->next;
		memcpy((char*)mem[cur->i] + remnant, mbuf, remvac);
		mbuf = mbuf + remvac;
		msize = msize - remvac;
		cur = cur->next;
	}
	else{
		for (int i = 0; i < start-1; i++) cur = cur->next;//search for the start block
		memcpy((char*)mem[cur->i] + remnant, mbuf, msize);
		
		return size;
	}
	while(msize>0) {
		
		if (msize > blocksize) {
			memcpy(mem[cur->i], mbuf, blocksize);
			mbuf = mbuf + blocksize;
			msize = msize - blocksize;
			cur = cur->next;
		}
		else {
			memcpy(mem[cur->i], mbuf, msize);
			break;
		}
	}
	
	return size;

}//oshfs_write

static int oshfs_truncate(const char *path, off_t size)
{
	struct filenode *node = get_filenode(path);
	if(node==NULL) return -1;
	int n = ((int)(size)-1)/ (int)(blocksize)+1;//n block reserved
	struct data_lnode* newtail;
	newtail = node->content;
	for (int i = 0; i < n - 1; i++) {
		newtail = newtail->next;
	}
	struct data_lnode *del;
	del = newtail->next;
	while (del != NULL) {
		munmap(mem[del->i], blocksize);
		mark_unused(del->i);
		del = del->next;//notice:because all the lnode list stay in one block,there is no need to release part of this block's space
	}
	newtail->next = NULL;
	node->st->st_size = size;//the security issue is that the actual data in the last block won't be removed.Only the tag changed.
	node->tail = newtail;

	return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{

	
	struct filenode *node = get_filenode(path);
	if(node==NULL) return 0;
	int ret = size;

	if(offset > node->st->st_size){
		perror("forbidden area");
		return -1;
	}
	if (offset + size > node->st->st_size)
		ret = node->st->st_size - offset;//note:consider the case where ret<offset
	
	int n = ((int)(offset)-1) /(int) (blocksize) + 1;//从第n块开始读(编号为n-1)

	struct data_lnode *cur;
	cur = node->content;
	for (int i = 0; i < n - 1; i++)
		cur = cur->next;
	int remnant, remvac;//对应的位置同write
	remnant = offset - (int)(blocksize)*(n - 1);
	remvac = blocksize - remnant;
	char* mbuf;
	mbuf=buf;
	int mret=ret;
	if(remvac<ret){
		memcpy(mbuf, mem[cur->i] + remnant, remvac);
		mbuf = mbuf + remvac;
		mret = mret - remvac;
		cur = cur->next;
	}
	else{

		memcpy(buf,mem[cur->i]+remnant,ret);
		return ret;
	}
	while (mret > 0) {
		if (mret > blocksize) {
			memcpy(mbuf, mem[cur->i], blocksize);
			mbuf = mbuf + blocksize;
			mret = mret - blocksize;
			cur = cur->next;
		}
		else {
			memcpy(mbuf, mem[cur->i], mret);
			break;
		}
	}
	return ret;
}//oshfs_read

static int oshfs_unlink(const char *path)
{
	// READY
	struct filenode *node = get_filenode(path);
        struct filenode *node_last=get_filenode_last(path);
	
	struct data_lnode *cur = node->content;
	while (cur != NULL) {
		munmap(mem[cur->i], blocksize);
		mark_unused(cur->i);
		cur = cur->next;
	}
	munmap(node->content, blocksize);
	mark_unused(find_address(node->content));
	//delete node.
	munmap(node->st, blocksize);//here,we can see a problem clearly:even the filename of a node will occupy
	mark_unused(find_address(node->st));
	if(node_last==NULL){
		root=NULL;
		munmap(node,blocksize);
		mark_unused(find_address(node));
		return 0;
	}
	else{
		node_last->next=node->next;
		munmap(node,blocksize);
		mark_unused(find_address(node));
	}
	return 0;
}

static const struct fuse_operations op = {
    //.init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);
}
