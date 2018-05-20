#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <inttypes.h>

#define blocknr 4*1024
#define blocksize 16*1024


typedef int addr;
struct filenode {
	char filename[256];
	//addr content[nptr];
	addr content;//�洢content ָ��ĵط�
	struct stat st;
	int num_blocks;
	//int num_content;
	struct filenode *next;
};

struct head_block{
	struct filenode* next;
	int last_applied;
	char used[blocknr];
};


static void *mem[blocknr];



int find_vacancy() {
	int i,j=0;
	struct head_block* root=(struct head_block*)mem[0];
	for(i=root->last_applied;root->used[i]!=0 && j<blocknr;i++,j++);
	if(j==blocknr) return 0;//no vacancy
	else{
		root->last_applied=i;
		return i;
	}
}


static void create_filenode(const char *filename, const struct stat *st) {
	int i;

	struct head_block* root=(struct head_block*)mem[0];
	struct filenode *newnode;
	
	//����ռ�
	i = find_vacancy();
	if(i==0) {
		perror("space full");
		return;
	}

	mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	newnode = (struct filenode*)mem[i];
	root->used[i]=1;

	//��ʼ�����ݣ���������һ������ָ���
	newnode->st.st_ino=i;
	newnode->st.st_size=0;
	memcpy(newnode->filename, filename, strlen(filename) + 1);
	memcpy(&(newnode->st) , st, sizeof(struct stat));
	int j;
	j=find_vacancy();
	if(j==0){
		perror("space full");
		return;
	}

	mem[j]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	*(root->used+j)=1;
	newnode->content=j;
	newnode->num_blocks=0;
	
	//�����ļ�������
	newnode->next=root->next;
	root->next=newnode;

	return;
}

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node = ((struct head_block*)mem[0])->next;
    while(node)
    {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

static struct filenode *get_filenode_last(const char *name)
{
	struct head_block *root=(struct head_block*)mem[0];
	struct filenode* node=root->next;
	struct filenode *node_last=NULL;
	while (node) {
		if (strcmp(node->filename, name + 1) != 0) {
			node_last = node;
			node = node->next;
		}
		else
			return node_last;
	}
	return node_last;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
	//��ʼ��������head_block
	mem[0]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	struct head_block* root;
	root=(struct head_block*)mem[0];
	root->next=NULL;
	//��ʼ���������ڵ�used����
	for(int i=0;i<blocknr;i++)
		root->used[i]=0;
	root->used[0]=1;
	root->last_applied=0;
	
	return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{

    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0)
    {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    }
    else if(node)
        memcpy(stbuf, &(node->st), sizeof(struct stat));
    else
        ret = -ENOENT;
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct head_block* root=(struct head_block*)mem[0];
    struct filenode *node = root->next;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node)
    {
        filler(buf, node->filename, &(node->st), 0);
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
	
	struct head_block* root=(struct head_block*)mem[0];
	struct filenode* node = get_filenode(path);
	addr* content=(addr*)mem[node->content];
	if (node == NULL) return 0;

	int bsize = blocksize*node->num_blocks;//bsize:���е����ݿ�������ɵ�������
	if (offset + size > bsize) {
		//������ݿ�
		int add_size = offset + size - bsize;
		int nb = (add_size - 1) / blocksize + 1;//��Ҫ��ӵ�����
		for(int i=node->num_blocks;i<node->num_blocks+nb;i++){
			int j;
			j=find_vacancy();
			if(j==0){
				perror("space full");
				return 0;
			}
			mem[j]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			root->used[j]=1;
			content[i]=j;
		}
		node->num_blocks+=nb;
	}
	
	//�����ϣ���ʼд��
	if(offset+size>node->st.st_size)
		node->st.st_size=offset+size;
		
	int offset_int=offset;
	int addr1=offset_int/blocksize;//��addr1�����ݿ�
	int addr2=offset_int%blocksize;//���һ�����ݿ��У���addr2���ֽ�
	int remain=blocksize-addr2;//���һ�����ݿ黹��������remain���ֽ�
	
	char *cur_buf=buf;
	int cur_size=size;
	if(size<=remain){//����Ҫ�µ����ݿ飬ֻ������ԭ�����һ�����ݿ��ʣ�ಿ��
		memcpy(mem[content[addr1]]+addr2,cur_buf,size);
		return size;
	}
	else{//��Ҫ�µ����ݿ飬ǰ���Ѿ��������
		memcpy(mem[content[addr1]]+addr2,cur_buf,remain);//������ԭ�������ݿ�
		cur_buf+=remain;
		cur_size-=remain;
		addr1+=1;
		while(cur_size>0){
			if(cur_size>=blocksize){//ʣ�����һ��
				memcpy(mem[content[addr1]],cur_buf,blocksize);
				cur_buf+=blocksize;
				cur_size-=blocksize;
				addr1+=1;//�����¸����ݿ�
			}
			else{//ʣ�಻��һ��
				memcpy(mem[content[addr1]],cur_buf,cur_size);
				break;
			}
		}
		return size;
	}

}

		
static int oshfs_truncate(const char *path, off_t size)
{
	struct head_block* root=(struct head_block*)mem[0];
	struct filenode *node = get_filenode(path);
	if (node == NULL) return 0;
	
	addr *content=(addr *)mem[node->content];
	
	int bsize=(size-1)/blocksize+1;
	
	for(int i=bsize;i<node->num_blocks;i++){
		munmap(mem[content[i]],blocksize);
		root->used[content[i]]=0;
	}
	
	node->num_blocks=bsize;
	node->st.st_size=size;
	return 0;
}




static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	//����ͬwrite��д�벿��
	struct head_block* root=(struct head_block*)mem[0];
	struct filenode *node = get_filenode(path);
	if (node == NULL) return 0;
	int ret = size;
	addr* content=(addr*)mem[node->content];
	
	if(offset>node->st.st_size){
		perror("forbidden visit");
		return 0;
	}
	if(offset+size>node->st.st_size)
		ret=node->st.st_size-offset;
	
	int offset_int=offset;
	int addr1=offset_int/blocksize;
	int addr2=offset_int%blocksize;
	int remain=blocksize-addr2;
	
	char *cur_buf=buf;
	int cur_size=ret;
	if(ret<=remain){
		memcpy(cur_buf,mem[content[addr1]]+addr2,size);
		return ret;
	}
	else{
		memcpy(cur_buf,mem[content[addr1]]+addr2,remain);
		cur_buf+=remain;
		cur_size-=remain;
		addr1+=1;
		while(cur_size>0){
			if(cur_size>=blocksize){
				memcpy(cur_buf,mem[content[addr1]],blocksize);
				cur_buf+=blocksize;
				cur_size-=blocksize;
				addr1+=1;
			}
			else{
				memcpy(cur_buf,mem[content[addr1]],cur_size);
				break;
			}
		}
		return ret;
	}
}


static int oshfs_unlink(const char *path)
{
	struct head_block* root=(struct head_block*)mem[0];
	struct filenode *node = get_filenode(path);
	if (node == NULL) return -1;//not found ,failure

	struct filenode *node_last = get_filenode_last(path);
	int p_volumn = blocksize / sizeof(int);
	addr* content=(addr*)mem[node->content];
	//�ͷ����ݿ�
	for(int i=0;i<node->num_blocks;i++){
		munmap(mem[content[i]],blocksize);
		root->used[content[i]]=0;
	}
	//�ͷ�����ָ���
	munmap(mem[node->content],blocksize);
	root->used[node->content]=0;
	
	//�ͷ��ļ���
	if(node_last==NULL){
		root->next=node->next;
		root->used[node->st.st_ino]=0;
		munmap(mem[node->st.st_ino],blocksize);
	}
	
	else{
		node_last->next=node->next;
		root->used[node->st.st_ino]=0;
		munmap(mem[node->st.st_ino],blocksize);
	}
	return 0;
}
		


static const struct fuse_operations op = {
        .init = oshfs_init,
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




