# 实验报告

## 实现功能

实现了基本的read,write,truncate,unlink,没有实现mkdir。文件系统的结构同助教的链表。存储结构为，用指针数组（事实上每个指针是一个指针结构体）指向内存中的每一块数据。
```Bash
$ cd mountpoint
$ ls -al
$ echo helloworld > testfile
$ ls -l testfile
$ cat testfile 
$ dd if=/dev/zero of=testfile bs=1M count=1000 //请注意！这里是1000，原因可参见注意事项中斜体部分
$ ls -l testfile
$ dd if=/dev/urandom of=testfile bs=1M count=1 seek=10
$ ls -l testfile # 此时应为11MiB
$ dd if=testfile of=/dev/null 
$ rm testfile
$ ls -al
```
*注意事项* 
          blocknr=16 * 1024(16k)

          blocksize=128 * 1024(128k),如此安排的原因是：
          
          本算法中，filenode结构为：
          
          ![]()
          content为一个记录一个文件中每一块的地址的伪链表，用数组存储。
          
          tail为这个伪链表的尾指针。
          
          一开始为该数组申请了一块block,所以数组中最多存入blocksize/sizeof (struct data_lnode) 个块指针结构data_lnode
          
          缺点在于，*限制块指针的数目就限制了一个文件的最大块数，从而限制文件大小，所以本次的文件系统虽然理论上有2G，但是可以存入的大文件只有1G*
          
          优点在于，1. 可以避免在每一块中开辟一个指向下一块的next指针，从而避免对齐的隐患问题
          
                  2. 全部存在一块中，是对指针最大程度的压缩，可以避免每一个很小的块指针结构占用一个相对很大的块，从而可以大量减少内部碎片（用指针数组存在filenode中固然也十分紧凑，但是这会造成filenode体积急剧增大，大于一个blocksize,而filenode的跨块存储是十分不便的）
                  
          可改进但时间不足所以没有做的地方：可以将block一开始就格式化为指针数组的结构，从而节约next指针的空间，因此可以多存一些块指针。
 


