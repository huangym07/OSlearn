//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// 0 -> succeed, -1 -> failed
uint64
vmaalloc(uint64 addr, uint64 length, int prot, int flags, int fd)
{
  struct proc *p = myproc();
  for (int i = 0; i < NPVMA; i++) {
    if (p->mvma[i].used == 0) {
      struct vma *vma = &p->mvma[i];
      vma->addr = addr;
      vma->newaddr = addr;
      vma->length = length;
      vma->permit = prot;
      vma->flags =flags;
      vma->used = 1;
      vma->f = p->ofile[fd];
      filedup(vma->f);
      return 0;
    }
  }
  return -1;
}

void
print(struct vma* vma)
{
  printf("vma: addr is %p, valid addr is %p, length is %d, prot is %x, file address is %p\n", vma->addr, vma->newaddr, vma->length, vma->permit, vma->f);
}

// Return address at which to map, 0xffffffffffffffff -> failed
uint64
sys_mmap(void)
{
  // to simplify, assume that addr is NULL, and offset is zero
  // and prot is either PROT_HEAD or PROT_WRITE (or both), and
  // flags is either MAP_SHARED or MAP_PRIVATE
  int length, prot, flags, fd;
  struct file *f;
  uint64 addr,  // address at which to map the file
    lenroundup; // PGROUNDUP(length)
  if (argint(1, &length) < 0)
    goto failed;
  if (length <= 0)
    goto failed;
  if (argint(2, &prot) < 0) 
    goto failed;
  if (argint(3, &flags) < 0)
    goto failed;
  if (argfd(4, &fd, &f) < 0)
    goto failed;

  // check permissions
  // printf("my checkpoint1:\n");
  // printf("length is %d, prot is %x, flags is %x, fd is %d\n", length, prot, flags, fd);

  if (filepermit(f, prot, flags) < 0) {
    // printf("mmap failed: file has no permissions.\n");
    goto failed;
  }
  // printf("filepermit passed\n");
  // printf("my checkpoint1 over.\n");

  // find an address at which to map the file,
  // and lazily allocate a multiple of the page size
  // which is just bigger than or equal to length
  addr = PGROUNDUP(myproc()->sz);
  lenroundup = PGROUNDUP((uint64)length);
  myproc()->sz += lenroundup;

  // allocate a unused vma
  if (vmaalloc(addr, lenroundup, prot, flags, fd) < 0)
    goto failed;
  // check vma
  // printf("my checkpoint2 for vma: \n");
  // printf("addr is %p, length is %d, prot is %x, file addr is %p\n", addr, length, prot, myproc()->ofile[fd]);
  // printf("now look through vmas: \n");
  // struct proc *p = myproc();
  // for (int i = 0; i < NPVMA; i++) {
  //   if (p->mvma[i].length > 0) 
  //     print(&p->mvma[i]);
  // }
  // printf("all used vmas are printed\n");
  // printf("my checkpoint2 over.\n");

  return addr;
failed:  
  return (uint64)-1;
}

extern struct vma* findvma(uint64 addr);

int
writeback(struct vma* vma, uint64 addr, uint64 len)
{
  pte_t *pte;
  struct file *f = vma->f;
  uint off;
  int r, ret = 0;
  for (uint64 p = addr; p < addr + len; p += PGSIZE) {
    if ((pte = walk(myproc()->pagetable, p, 0)) == 0 || (*pte & PTE_V) == 0) {
      // printf("writeback: no physical page, va is %p\n", p);
      continue;
    }
    // writeback to file should not change f->off, 
    // because there might be more than one vma corresponding to the same file
    // and system call write can also change f->off
    off = p - vma->addr;
    int n = PGSIZE;
    if(f->type == FD_PIPE){
      ret = pipewrite(f->pipe, p, n);
    } else if(f->type == FD_DEVICE){
      if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
        return -1;
      ret = devsw[f->major].write(1, p, n);
    } else if(f->type == FD_INODE){
      // write a few blocks at a time to avoid exceeding
      // the maximum log transaction size, including
      // i-node, indirect block, allocation blocks,
      // and 2 blocks of slop for non-aligned writes.
      // this really belongs lower down, since writei()
      // might be writing a device like the console.
      int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
      int i = 0;
      // printf("writeback: f->type == FD_INODE\n");
      while(i < n){
        int n1 = n - i;
        if(n1 > max)
          n1 = max;

        begin_op();
        ilock(f->ip);
        if ((r = writei(f->ip, 1, p + i, off, n1)) > 0)
          off += r;
        iunlock(f->ip);
        end_op();

        if(r != n1){
          // error from writei
          break;
        }
        i += r;
      }
      ret = (i == n ? n : -1);
      if (ret < 0) 
        return -1;
    } else {
      panic("writeback");
    }
  }
  return 0;
}

int
munmap(uint64 addr, uint64 length)
{
  // printf("munmap: addr is %p, length is %d\n", addr, length);
  struct vma* vma = findvma(addr);
  // printf("munmap: vma is %p, pid=%d\n", vma, myproc()->pid);
  if (vma == 0) 
    return -1;

  // printf("munmap: checkpoint 1\n");
  uint64 len = PGROUNDUP(length);
  if (vma->newaddr == addr) {
    // printf("munmap: checkpoint first 2\n");
    // unmap head of vma
    uint64 size = len >= vma->length ? vma->length : len;
    // printf("munmap: first size is %d\n", size);
    // write back if MAP_SHARED and page is allocated and maped into address space
    if ((vma->flags & MAP_SHARED) && (vma->permit & PROT_WRITE) && writeback(vma, vma->newaddr, size) < 0) {
      // printf("munmap: writeback failed\n");
      return -1;
      // uint64 size2 = filewrite(vma->f, vma->newaddr, size);
      // printf("munmap: second size is %d\n", size2);
      // if (size != size2)
      //   return -1;
    }
    // printf("munmap: checkpoint 2\n");
    if (vma->length <= len) {
      // whole vma is unmaped
      // decrease the ref count of the file by 1
      fileclose(vma->f);
      // free memory
      uvmunmap(myproc()->pagetable, vma->newaddr, vma->length / PGSIZE, 1);
      // change vma
      vma->addr = 0;
      vma->newaddr = 0;
      vma->length = 0;
      vma->used = 0;
      vma->permit = 0;
      vma->flags = 0;
      vma->f = 0;
    } else {
      uvmunmap(myproc()->pagetable, vma->newaddr, len / PGSIZE, 1);
      vma->newaddr += len;
      vma->length -= len;
    }
  } else {
    // printf("munmap: checkpoint first 3\n");
    // unmap tail of vma
    if (addr + len >= vma->newaddr + vma->length) 
      len = vma->newaddr + vma->length - addr;
    // write back if MAP_SHARED
    if ((vma->flags & MAP_SHARED) && (vma->permit & PROT_WRITE) && writeback(vma, addr, len) < 0) 
      return -1;
    // printf("munmap: checkpoint 3\n");
    uvmunmap(myproc()->pagetable, addr, len / PGSIZE, 1);
    vma->length = addr - vma->newaddr;
  }
  return 0;
}

// 0 -> succeed, -1 -> failed
uint64
sys_munmap(void)
{
  uint64 addr;
  int length;

  if (argaddr(0, &addr) < 0)
    return -1;
  if (argint(1, &length) < 0)
    return -1;
  
  int ret = munmap(addr, length);
  // printf("sys_munmap: ret is %d\n", ret);
  return (uint64)ret;
}