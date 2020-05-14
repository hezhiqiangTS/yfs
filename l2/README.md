## 概览

* FUSE 接口：fuse.cc，作用是将FUSE内核模块中的FUSE操作转换为YFS客户端调用。
* YFS 客户端：与典型的网络文件系统客户端不同，yfs_client 实际上执行的是文件系统的逻辑。比如，当创建一个新文件时，yfs_client 必须亲自在 directory block 中添加 directory entries(传统文件系统中，是由服务端执行该任务的)。为了获取以及保存包含了文件数据或者director entries的数据块，yfs_client需要和extend server通信。因此，yfs_client需要知道如何理解以及管理extents。

Extent Server是文件系统中所有数据的一个中心化的存储地点。Extent Server 的代码分为两个主要部分：
* Extent client. 将通过 rpc 与 extent server进行通信的方法封装为一个类
* Extent server. extent server作用是一个简单的key-value存储。Extent server仅仅将整个文件作为 strings 来保存。同时还会保存文件的属性信息。
```bash
cd yfs/l2
./extent_server 3772 &

mkdir myfs
./yfs_client ./myfs 3772 3762 &
```

Skeleton Code 仅仅实现了 GETATTR 和 STATFS 操作。

## Your Job
你的任务是实现LOOKUP，CREATE/MKNOD，以及 READDIR 这三个 FUSE 操作。你需要将文件系统的内容保存在extent server中，这样未来的试验中，你可以在多个server中共享文件系统。

在使用Linux上的FUSE时，正如在官方编程环境中一样，文件是通过MKNOD操作创建的。

## Detailed Guidance
* 决定文件系统的表现形式
YFS应该对所有文件和目录用一个unique identifier来命名（就像on-disk UNIX文件系统中的i-node number一样）。我们在 yfs_client.cc 中定义了一个 64 位的 identifier（called inom）。由于FUSE使用unique 32-bit identifier来获取文件系统中的文件和目录，我们建议你使用inum的至少32位来作为对应的fuse identifier

当创建新文件或者目录时，你需要为它赋一个值inum。最简单的方式是使用一个随机数，同时祈祷它是唯一的。

YFS 需要辨别一个特定的inum对应与文件还是目录。为了实现这个目标，你需要保证任意文件的32位FUSE identifier中有一个重要的标志位为1；对应的，文件的标志位为0.

然后，你需要选择用于保存和检索文件系统元数据（比如文件/目录的格式，以及目录内容）的格式。文件或者目录的属性包含由比如文件的长度、修改次数。目录的内容包含list of name 到 inum 的映射。因此，解析一个文件的inum会导致从系统根目录开始的一系列查询。

将目录内容或者文件内容保存在extent server中会带来方便。extent server以文件/目录的 inum 作为key。然而，由于YFS必须同样能够基于inum来获取文件属性信息，我们需要实现一个单独的 extent_server::getattr(key) 函数，作用是基于inum来检索文件或者目录的属性。
The attribute consists of the file size, last modification time (mtime), change time (ctime), and last access time (atime). 

* 实现 FUSE 文件系统
