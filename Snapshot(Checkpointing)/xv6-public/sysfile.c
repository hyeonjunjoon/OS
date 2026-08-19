#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "buf.h"
extern struct superblock sb;



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



static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
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

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
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

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}


int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
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


static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}


int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  
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
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
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
    if(type == T_FILE && ip->type == T_FILE)
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

  if(type == T_DIR){  
    dp->nlink++;  
    iupdate(dp);
    
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
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

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
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
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}


static void
copy_file(struct inode *src, struct inode *dst)
{
  dst->type = src->type;
  dst->major = src->major;
  dst->minor = src->minor;
  dst->size = src->size;
  memmove(dst->addrs, src->addrs, sizeof(src->addrs));
  
  for(int i=0; i<NDIRECT; i++){
    if(src->addrs[i]) bref(src->dev, src->addrs[i]);
  }
  if(src->addrs[NDIRECT]){
    bref(src->dev, src->addrs[NDIRECT]);
    struct buf *bp = bread(src->dev, src->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    for(int j=0; j<NINDIRECT; j++){
      if(a[j]) bref(src->dev, a[j]);
    }
    brelse(bp);
  }
  
  iupdate(dst);
}

static void
copy_dir_recursive(struct inode *src, struct inode *dst)
{
  struct dirent de;
  struct inode *ip, *new_ip;
  int off = 0;
  
  while(1){
    ilock(src);
    int n = readi(src, (char*)&de, off, sizeof(de));
    iunlock(src);
    
    if(n != sizeof(de)) break;
    off += sizeof(de);
    
    if(de.inum == 0) continue;
    if(namecmp(de.name, ".") == 0 || namecmp(de.name, "..") == 0) continue;
    if(namecmp(de.name, "snapshot") == 0) continue;
    
    ip = iget(src->dev, de.inum);
    ilock(ip);
    
    if(ip->type == T_DEV){
      iunlockput(ip);
      continue;
    }
    
    new_ip = ialloc(dst->dev, ip->type);
    ilock(new_ip);
    
    if(ip->type == T_DIR){
      new_ip->major = 0;
      new_ip->minor = 0;
      new_ip->nlink = 1;
      iupdate(new_ip);
      
      ilock(dst);
      dst->nlink++;
      iupdate(dst);
      dirlink(dst, de.name, new_ip->inum);
      iunlock(dst);
      
      dirlink(new_ip, ".", new_ip->inum);
      dirlink(new_ip, "..", dst->inum);
      iunlock(new_ip);
      
      iunlock(ip);
      copy_dir_recursive(ip, new_ip);
      ilock(ip);
      iput(new_ip);
    } else {
      new_ip->nlink = 1;
      copy_file(ip, new_ip);
      iunlock(new_ip);
      
      ilock(dst);
      dirlink(dst, de.name, new_ip->inum);
      iunlock(dst);
      
      iput(new_ip);
    }
    
    iunlockput(ip);
  }
}

static void
delete_content_recursive(struct inode *dp)
{
  struct dirent de;
  struct inode *ip;
  int off = 0;
  
  while(1){
    ilock(dp);
    int n = readi(dp, (char*)&de, off, sizeof(de));
    if(n != sizeof(de)){
      iunlock(dp);
      break;
    }
    
    if(de.inum == 0){
      off += sizeof(de);
      iunlock(dp);
      continue;
    }
    if(namecmp(de.name, ".") == 0 || namecmp(de.name, "..") == 0){
      off += sizeof(de);
      iunlock(dp);
      continue;
    }
    if(namecmp(de.name, "snapshot") == 0){
      off += sizeof(de);
      iunlock(dp);
      continue;
    }
    
    
    uint inum = de.inum;
    memset(&de, 0, sizeof(de));
    writei(dp, (char*)&de, off, sizeof(de));
    iunlock(dp);
    
    ip = iget(dp->dev, inum);
    ilock(ip);
    if(ip->type == T_DIR){
      delete_content_recursive(ip);
    }
    
    if(ip->nlink > 0) ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    
    off += sizeof(de);
  }
}

int
sys_snapshot_create(void)
{
  struct inode *root, *snap_root, *snap_dir;
  int id = 1;
  
  begin_op();
  
  snap_root = namei("/snapshot");
  if(snap_root){
    ilock(snap_root);
    if(snap_root->type != T_DIR){
      iunlockput(snap_root);
      end_op();
      cprintf("snap_create: /snapshot exists but is not a directory\n");
      return -1;
    }
    iunlock(snap_root);
  } else {
    root = namei("/");
    ilock(root);
    snap_root = ialloc(root->dev, T_DIR);
    ilock(snap_root);
    snap_root->major = 0;
    snap_root->minor = 0;
    snap_root->nlink = 1;
    iupdate(snap_root);
    
    dirlink(snap_root, ".", snap_root->inum);
    dirlink(snap_root, "..", root->inum);
    iunlock(snap_root);
    
    dirlink(root, "snapshot", snap_root->inum);
    root->nlink++;
    iupdate(root);
    iunlockput(root);
  }
  
  
  struct inode *refs = namei("/snapshot/refs");
  if(refs == 0){
    ilock(snap_root);
    refs = ialloc(snap_root->dev, T_FILE);
    ilock(refs);
    refs->major = 0;
    refs->minor = 0;
    refs->nlink = 1;
    iupdate(refs);
    dirlink(snap_root, "refs", refs->inum);
    iunlock(snap_root);
    
    iunlockput(refs);
    
    
    
    update_ref_ip();
  } else {
    iput(refs);
    update_ref_ip();
  }
  
  struct dirent de;
  int off = 0;
  int max_id = 0;
  ilock(snap_root);
  while(readi(snap_root, (char*)&de, off, sizeof(de)) == sizeof(de)){
    off += sizeof(de);
    if(de.inum == 0) continue;
    if(namecmp(de.name, ".")==0 || namecmp(de.name, "..")==0) continue;
    if(namecmp(de.name, "refs")==0) continue;
    int curr_id = 0;
    char *s = de.name;
    while(*s >= '0' && *s <= '9'){
      curr_id = curr_id * 10 + (*s - '0');
      s++;
    }
    if(curr_id > max_id) max_id = curr_id;
  }
  id = max_id + 1;
  
  char id_str[16];
  int temp = id;
  int len = 0;
  if(temp == 0) { id_str[0]='0'; len=1; }
  else {
    while(temp > 0) { len++; temp /= 10; }
    temp = id;
    for(int i=len-1; i>=0; i--){
      id_str[i] = (temp % 10) + '0';
      temp /= 10;
    }
  }
  id_str[len] = 0;
  
  
  snap_dir = ialloc(snap_root->dev, T_DIR);
  if(snap_dir == 0){
    iunlockput(snap_root);
    return -1;
  }
  ilock(snap_dir);
  snap_dir->major = 0;
  snap_dir->minor = 0;
  snap_dir->nlink = 1;
  iupdate(snap_dir);
  dirlink(snap_dir, ".", snap_dir->inum);
  dirlink(snap_dir, "..", snap_root->inum);
  iunlock(snap_dir);
  
  
  dirlink(snap_root, id_str, snap_dir->inum);
  snap_root->nlink++;
  iupdate(snap_root);
  iunlockput(snap_root);
  
  
  root = namei("/");
  copy_dir_recursive(root, snap_dir);
  iput(root);
  iput(snap_dir);
  
  end_op();
  return id;
}

int
sys_snapshot_rollback(void)
{
  int id;
  if(argint(0, &id) < 0) return -1;
  
  begin_op();
  
  char path[32];
  
  
  char id_str[16];
  int temp = id;
  int len = 0;
  if(temp == 0) { id_str[0]='0'; len=1; }
  else {
    while(temp > 0) { len++; temp /= 10; }
    temp = id;
    for(int i=len-1; i>=0; i--){
      id_str[i] = (temp % 10) + '0';
      temp /= 10;
    }
  }
  id_str[len] = 0;
  
  
  char *prefix = "/snapshot/";
  int i=0;
  for(; prefix[i]; i++) path[i] = prefix[i];
  for(int j=0; j<len; j++) path[i++] = id_str[j];
  path[i] = 0;
  
  struct inode *snap_dir = namei(path);
  if(snap_dir == 0){
    end_op();
    return -1;
  }
  
  struct inode *root = namei("/");
  
  
  delete_content_recursive(root);
  
  
  copy_dir_recursive(snap_dir, root);
  
  iput(root);
  iput(snap_dir);
  
  end_op();
  return 0;
}

int
sys_snapshot_delete(void)
{
  int id;
  if(argint(0, &id) < 0) return -1;
  
  begin_op();
  
  char path[32];
  char id_str[16];
  int temp = id;
  int len = 0;
  if(temp == 0) { id_str[0]='0'; len=1; }
  else {
    while(temp > 0) { len++; temp /= 10; }
    temp = id;
    for(int i=len-1; i>=0; i--){
      id_str[i] = (temp % 10) + '0';
      temp /= 10;
    }
  }
  id_str[len] = 0;
  
  char *prefix = "/snapshot/";
  int i=0;
  for(; prefix[i]; i++) path[i] = prefix[i];
  for(int j=0; j<len; j++) path[i++] = id_str[j];
  path[i] = 0;
  
  struct inode *snap_dir = namei(path);
  if(snap_dir == 0){
    cprintf("snap_delete: snap_dir not found\n");
    end_op();
    return -1;
  }
  
  
  delete_content_recursive(snap_dir);
  
  
  struct inode *snap_root = namei("/snapshot");
  
  
  struct dirent de;
  int off = 0;
  ilock(snap_root);
  while(readi(snap_root, (char*)&de, off, sizeof(de)) == sizeof(de)){
    if(de.inum == snap_dir->inum){
      memset(&de, 0, sizeof(de));
      writei(snap_root, (char*)&de, off, sizeof(de));
      break;
    }
    off += sizeof(de);
  }
  snap_root->nlink--; 
  iupdate(snap_root);
  iunlockput(snap_root);
  
  ilock(snap_dir);
  snap_dir->nlink = 0; 
  iupdate(snap_dir);
  iunlockput(snap_dir);
  
  end_op();
  return 0;
}

int
sys_get_block_addr(void)
{
  int fd;
  int bn;
  struct file *f;
  
  if(argfd(0, &fd, &f) < 0 || argint(1, &bn) < 0)
    return -1;
    
  struct inode *ip = f->ip;
  ilock(ip);
  
  uint addr = 0;
  if(bn == -1){
    addr = ip->addrs[NDIRECT];
  } else if(bn < NDIRECT){
    addr = ip->addrs[bn];
  } else {
    bn -= NDIRECT;
    if(bn < NINDIRECT){
      uint iaddr = ip->addrs[NDIRECT];
      if(iaddr){
        struct buf *bp = bread(ip->dev, iaddr);
        uint *a = (uint*)bp->data;
        addr = a[bn];
        brelse(bp);
      }
    }
  }
  
  iunlock(ip);
  return addr;
}
