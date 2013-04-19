#include "maple.h"
#include "vmsfs.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

extern void *memcpy(void *s1, const void *s2, unsigned int n);
extern void *memset(void *s, int c, unsigned int n);
extern int memcmp(const void *s1, const void *s2, unsigned int n);


static unsigned int tmppkt[256];
static unsigned char tmpblk[8192];
static struct dir_iterator tmpiter;
static struct dir_entry tmpentry;


unsigned int read_belong(unsigned int *l)
{
  unsigned char *b = (unsigned char *)l;
  return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
}

void write_belong(unsigned int *l, unsigned int v)
{
  unsigned char *b = (unsigned char *)l;
  b[0] = v>>24;
  b[1] = v>>16;
  b[2] = v>>8;
  b[3] = v;
}

static int tobcd(int n)
{
  int dec = n/10;
  return (dec<<4)|(n-10*dec);
}

static int frombcd(int n)
{
  return 10*((n>>4)&15)+(n&15);
}

void vmsfs_timestamp_to_bcd(unsigned char *bcd, struct timestamp *tstamp)
{
  int century = tstamp->year/100;
  bcd[0] = tobcd(century);
  bcd[1] = tobcd(tstamp->year-100*century);
  bcd[2] = tobcd(tstamp->month);
  bcd[3] = tobcd(tstamp->day);
  bcd[4] = tobcd(tstamp->hour);
  bcd[5] = tobcd(tstamp->minute);
  bcd[6] = tobcd(tstamp->second);
  bcd[7] = tobcd(tstamp->wkday);
}

void vmsfs_timestamp_from_bcd(struct timestamp *tstamp, unsigned char *bcd)
{
  tstamp->year = frombcd(bcd[0])*100+frombcd(bcd[1]);
  tstamp->month = frombcd(bcd[2]);
  tstamp->day = frombcd(bcd[3]);
  tstamp->hour = frombcd(bcd[4]);
  tstamp->minute = frombcd(bcd[5]);
  tstamp->second = frombcd(bcd[6]);
  tstamp->wkday = frombcd(bcd[7]);
}

int vmsfs_check_unit(int unit, int part, struct vmsinfo *info)
{
  unsigned int func;
  unsigned char *res;

  info->port = unit/6;
  info->dev = unit - info->port*6;
  info->pt = part;

  if(info->port<0 || info->port>3 || info->dev<0 || info->dev>5 || info->pt<0 || info->pt>255)
    return 0;

  res = maple_docmd(info->port, info->dev, MAPLE_COMMAND_DEVINFO, 0, NULL);

  if(res && res[0] == MAPLE_RESPONSE_DEVINFO && res[3]>=28 &&
     ((func=read_belong((unsigned int *)(res+4)))&MAPLE_FUNC_MEMCARD)) {
    int p=0;
    unsigned int func_data, param[2];

    info->func = func;

    for(func_data=0x80000000; func_data!=MAPLE_FUNC_MEMCARD; func_data>>=1)
      if(func & func_data)
	p++;
    func_data = read_belong(((unsigned int *)(res+8))+p);

    info->partitions = ((func_data>>24)&255)+1;
    info->blocksz = (((func_data>>16)&255)+1)<<5;
    info->writecnt = (func_data>>12)&15;
    info->readcnt = (func_data>>8)&15;
    info->removable = (func_data>>7)&1;

    if(part >= info->partitions)
      return 0;

    param[0] = MAPLE_FUNC_MEMCARD<<24;
    param[1] = part;

    usleep(20000);

    res = maple_docmd(info->port, info->dev, MAPLE_COMMAND_GETMINFO, 2, param);

    if(res && res[0] == MAPLE_RESPONSE_DATATRF && res[3]>=7 &&
       read_belong((unsigned int *)(res+4)) == MAPLE_FUNC_MEMCARD) {
      struct maple_minfo *minfo = (struct maple_minfo *)(res + 8);
      info->root_loc = minfo->root_loc;
      info->fat_loc = minfo->fat_loc;
      info->fat_size = minfo->fat_size;
      info->dir_loc = minfo->dir_loc;
      info->dir_size = minfo->dir_size;
      info->icon_shape = minfo->icon_shape;
      info->num_blocks = minfo->num_blocks;

      /* FIXME?  Can't handle cards with fat size != 1 */
      return info->fat_size == 1;

    } else
      return 0;
  } else
    return 0;
}

int vmsfs_beep(struct vmsinfo *info, int on)
{
  unsigned int param[2];
  unsigned char *res;
  int retr;

  if(!(info->func & MAPLE_FUNC_CLOCK))
    return 0;

  for(retr = 0; retr < 5; retr++) {

    write_belong(&param[0], MAPLE_FUNC_CLOCK);
    write_belong(&param[1], (on? 0xc0800000 : 0));

    if((res = maple_docmd(info->port, info->dev, MAPLE_COMMAND_SETCOND, 2, param))
       && res[0] == MAPLE_RESPONSE_OK)
      return 1;
  }

  return 0;
}

int vmsfs_read_block(struct vmsinfo *info, unsigned int blk, unsigned char *ptr)
{
  int retr, phase;
  unsigned char *res;
  unsigned int param[2];
  int subblk = info->blocksz/info->readcnt;

  if(blk>=0x10000)
    return 0;

  for(retr = 0; retr < 5; retr++) {
    for(phase = 0; phase < info->readcnt; phase++) {
      write_belong(&param[0], MAPLE_FUNC_MEMCARD);
      write_belong(&param[1], (info->pt<<24)|(phase<<16)|blk);
      if((res = maple_docmd(info->port, info->dev,
			    MAPLE_COMMAND_BREAD, 2, param)) &&
	 res[0] == MAPLE_RESPONSE_DATATRF && res[3] >= 2+(subblk>>2) &&
	 read_belong((unsigned int *)(res+4)) == MAPLE_FUNC_MEMCARD &&
	 read_belong((unsigned int *)(res+8)) == (info->pt<<24)|(phase<<16)|blk)
	memcpy(ptr+subblk*phase, res+12, subblk);
      else
	break;
    }
    if(phase >= info->readcnt)
      return 1;
  }
  return 0;
}

static int vmsfs_verify_block(struct vmsinfo *info, unsigned int blk, unsigned char *ptr)
{
  int retr, phase;
  unsigned char *res;
  unsigned int param[2];
  int subblk = info->blocksz/info->readcnt;

  if(blk>=0x10000)
    return 0;

  for(retr = 0; retr < 5; retr++) {
    for(phase = 0; phase < info->readcnt; phase++) {
      write_belong(&param[0], MAPLE_FUNC_MEMCARD);
      write_belong(&param[1], (info->pt<<24)|(phase<<16)|blk);
      if((res = maple_docmd(info->port, info->dev,
			    MAPLE_COMMAND_BREAD, 2, param)) &&
	 res[0] == MAPLE_RESPONSE_DATATRF && res[3] >= 2+(subblk>>2) &&
	 read_belong((unsigned int *)(res+4)) == MAPLE_FUNC_MEMCARD &&
	 read_belong((unsigned int *)(res+8)) == (info->pt<<24)|(phase<<16)|blk) {
	if(memcmp(ptr+subblk*phase, res+12, subblk))
	  return 0;
      }
      else
	break;
    }
    if(phase >= info->readcnt)
      return 1;
  }
  return 0;
}

int vmsfs_write_block(struct vmsinfo *info, unsigned int blk, unsigned char *ptr)
{
  int retr, phase;
  unsigned char *res;
  unsigned int param[2];
  int subblk = ((info->blocksz/info->writecnt)+3)>>2;

  if(subblk>253)
    subblk = 253;

  if(blk>=0x10000)
    return 0;

  for(retr = 0; retr < 5; retr++) {
    for(phase = 0; phase < info->writecnt; phase++) {
      int wt = 100;
      write_belong(&tmppkt[0], MAPLE_FUNC_MEMCARD);
      write_belong(&tmppkt[1], (info->pt<<24)|(phase<<16)|blk);
      memcpy(tmppkt+2, ptr+(subblk<<2)*phase, subblk<<2);
      if((res = maple_docmd(info->port, info->dev,
			    MAPLE_COMMAND_BWRITE, subblk+2, tmppkt))==NULL ||
	 res[0] != MAPLE_RESPONSE_OK)
	break;
      usleep(10000);
    }
    if(phase >= info->writecnt && vmsfs_verify_block(info, blk, ptr))
      return 1;
  }
  return 0;
}

int vmsfs_get_superblock(struct vmsinfo *info, struct superblock *s)
{
  int i;

  s->info = info;
  if(!vmsfs_read_block(info, info->root_loc, s->root))
    return 0;
  for(i=0; i<16; i++)
    if(s->root[i] != 0x55)
      return 0;
  if(!vmsfs_read_block(info, info->fat_loc, s->fat))
    return 0;
  s->root_modified=0;
  s->fat_modified=0;
  return 1;
}

int vmsfs_sync_superblock(struct superblock *s)
{
  if(s->fat_modified &&
     !vmsfs_write_block(s->info, s->info->fat_loc, s->fat))
    return 0;
  s->fat_modified = 0;
  if(s->root_modified &&
     !vmsfs_write_block(s->info, s->info->root_loc, s->root))
    return 0;
  s->root_modified = 0;
  return 1;
}

unsigned int vmsfs_get_fat(struct superblock *s, unsigned int n)
{
  n<<=1;
  if(n>=s->info->blocksz) return 0xfffc;
  return s->fat[n]|(s->fat[n+1]<<8);
}

void vmsfs_set_fat(struct superblock *s, unsigned int n, unsigned int l)
{
  n<<=1;
  if(n>=s->info->blocksz) return;
  s->fat[n]=l&0xff;
  s->fat[n+1]=(l>>8)&0xff;
  s->fat_modified = 1;
}

int vmsfs_count_free(struct superblock *s)
{
  int i, n = 0;
  for(i=0; i<s->info->num_blocks; i++)
    if(vmsfs_get_fat(s, i)==0xfffc)
      n++;
  return n;
}

int vmsfs_find_free_block(struct superblock *s)
{
  int i;
  for(i=s->info->num_blocks-1; i>=0; --i)
    if(vmsfs_get_fat(s, i)==0xfffc)
      return i;
  return 0xfffc;
}

void vmsfs_open_dir(struct superblock *s, struct dir_iterator *i)
{
  memset(i, 0, sizeof(struct dir_iterator));
  i->next_blk = s->info->dir_loc;
  i->blks_left = s->info->dir_size;
  i->dcnt = 999;
  i->super = s;
}

int vmsfs_next_dir_entry(struct dir_iterator *i, struct dir_entry *d)
{
  d->dir = i;
  if(i->dcnt >= (i->super->info->blocksz>>5)-1) {
    i->this_blk = i->next_blk;
    if(i->this_blk == 0xfffa || i->this_blk == 0xfffc || !i->blks_left--)
      return 0;
    if(!vmsfs_read_block(i->super->info, i->this_blk, i->blk))
      return 0;
    i->next_blk = vmsfs_get_fat(i->super, i->this_blk);
    i->dcnt = -1;
  }
  memcpy(d->entry, &i->blk[++i->dcnt*0x20], 0x20);
  d->dblk = i->this_blk;
  d->dpos = i->dcnt;
  return 1;
}

int vmsfs_next_named_dir_entry(struct dir_iterator *i, struct dir_entry *d, char *name)
{
  while(vmsfs_next_dir_entry(i, d))
    if(d->entry[0] && !strncmp(d->entry+4, name, 12))
      return 1;
  return 0;
}

int vmsfs_next_empty_dir_entry(struct dir_iterator *i, struct dir_entry *d)
{
  while(vmsfs_next_dir_entry(i, d))
    if(!d->entry[0])
      return 1;
  return 0;
}

int vmsfs_write_dir_entry(struct dir_entry *d)
{
  if(!vmsfs_read_block(d->dir->super->info, d->dblk, tmpblk))
    return 0;
  memcpy(tmpblk+d->dpos*0x20, d->entry, 0x20);
  return vmsfs_write_block(d->dir->super->info, d->dblk, tmpblk);
}

int vmsfs_open_file(struct superblock *super, char *name,
		    struct vms_file *file)
{

  file->super = super;
  vmsfs_open_dir(super, &tmpiter);

  if(vmsfs_next_named_dir_entry(&tmpiter, &tmpentry, name)) {
    int loc = tmpentry.entry[2]|(tmpentry.entry[3]<<8);
    int header_loc = loc;
    int header_read = 0;
    int header_sz, tmpsz;
    int i = tmpentry.entry[0x1a]|(tmpentry.entry[0x1b]<<8);
    int header_pos = i*super->info->blocksz;

    if(tmpentry.entry[0] == 0xcc)
      i = 1;
    else
      i = 0;
    file->loc0 = loc;
    file->blks = tmpentry.entry[0x18]|(tmpentry.entry[0x19]<<8);
    while(i--)
      header_loc = vmsfs_get_fat(super, header_loc);
    loc = header_loc;

    while(header_read < 0x80) {
      if(header_read)
	loc = vmsfs_get_fat(super, loc);
      if(loc == 0xfffa || loc == 0xfffc)
	return 0;
      if(!vmsfs_read_block(super->info, loc, file->blk+header_read))
	return 0;
      header_read += super->info->blocksz;
    }
    memcpy(&file->header, file->blk, 0x80);
    header_sz = 128 + (file->header.numicons<<9);
    switch(file->header.eyecatchtype) {
     case 1: header_sz += 8064; break;
     case 2: header_sz += 4544; break;
     case 3: header_sz += 2048; break;
    }
    file->size = file->header.filesize;
    tmpsz = file->blks * super->info->blocksz - header_pos - header_sz;
    if(!file->size || file->size > tmpsz)
      file->size = tmpsz;
    file->realsize = tmpsz;
    i = super->info->blocksz < 0x80;
    while(header_sz >= header_read) {
      header_read += super->info->blocksz;
      loc = vmsfs_get_fat(super, loc);
      if(loc == 0xfffa || loc == 0xfffc)
	return 0;
      i = 1;
    }
    if(i)
      if(!vmsfs_read_block(super->info, loc, file->blk))
	return 0;
    file->loc = loc;
    file->offs = super->info->blocksz - (header_read - header_sz);
    file->left = file->size;
    return 1;
  } else
    return 0;
}

int vmsfs_read_file(struct vms_file *file, unsigned char *buf,
		    unsigned int cnt)
{
  int bleft;

  if(cnt > file->left)
    return 0;

  if(!cnt)
    return 1;

  bleft = file->super->info->blocksz - file->offs;

  if(bleft) {
    if(bleft > cnt)
      bleft = cnt;
    memcpy(buf, file->blk + file->offs, bleft);
    file->offs += bleft;
    file->left -= bleft;
    cnt -= bleft;
    buf += bleft;
  }

  if(!cnt)
    return 1;

  while(cnt >= file->super->info->blocksz) {
    int newloc = vmsfs_get_fat(file->super, file->loc);
    if(newloc == 0xfffa || newloc == 0xfffc ||
       !vmsfs_read_block(file->super->info, newloc, buf))
      return 0;
    file->loc = newloc;
    file->left -= file->super->info->blocksz;
    cnt -= file->super->info->blocksz;
    buf += file->super->info->blocksz;
  }

  if(cnt) {
    int newloc = vmsfs_get_fat(file->super, file->loc);
    if(newloc == 0xfffa || newloc == 0xfffc ||
       !vmsfs_read_block(file->super->info, newloc, file->blk))
      return 0;
    memcpy(buf, file->blk, cnt);
    file->loc = newloc;
    file->offs = cnt;
    file->left -= cnt;
  }

  return 1;
}

static int calc_crc(const unsigned char *buf, int size, int n)
{
  int i, c;
  for (i = 0; i < size; i++)
  {
    n ^= (buf[i]<<8);
    for (c = 0; c < 8; c++)
      if (n & 0x8000)
        n = (n << 1) ^ 4129;
      else
        n = (n << 1);
  }
  return n & 0xffff;
}

int vmsfs_create_file(struct superblock *super, char *name,
		      struct vms_file_header *header,
		      void *icons, void *eyecatch,
		      void *data, unsigned long datasize,
		      struct timestamp *tstamp)
{
  unsigned char *header_ptr = (unsigned char *)header;
  unsigned char *icon_ptr = icons;
  unsigned char *eye_ptr = eyecatch;
  unsigned char *data_ptr = data;
  unsigned long headersize = 128, iconsize=(header->numicons)<<9, eyesize;
  unsigned long padsize, totsize;
  int blkcnt, freecnt = vmsfs_count_free(super);
  unsigned int lastblk, currblk = 0xfffc, nextblk = 0xfffc;

  header->filesize = datasize;
  switch(header->eyecatchtype) {
   case 1: eyesize = 8064; break;
   case 2: eyesize = 4544; break;
   case 3: eyesize = 2048; break;
   default: eyesize = 0; header->eyecatchtype = 0; break;
  }
  totsize = headersize + iconsize + eyesize + datasize;
  blkcnt = (totsize + super->info->blocksz - 1) / super->info->blocksz;
  padsize = blkcnt * super->info->blocksz - totsize;
  header->crc = 0;
  header->crc = calc_crc(data_ptr, datasize,
			 calc_crc(eye_ptr, eyesize,
				  calc_crc(icon_ptr, iconsize,
					   calc_crc(header_ptr, headersize,
						    0))));
  vmsfs_open_dir(super, &tmpiter);
  if(vmsfs_next_named_dir_entry(&tmpiter, &tmpentry, name)) {
    nextblk = tmpentry.entry[2] | (tmpentry.entry[3]<<8);
    freecnt += tmpentry.entry[0x18] | (tmpentry.entry[0x19]<<8);
  } else {
    vmsfs_open_dir(super, &tmpiter);
    if(!vmsfs_next_empty_dir_entry(&tmpiter, &tmpentry))
      return 0;
    tmpentry.entry[2] = 0xfa;
    tmpentry.entry[3] = 0xff;
    memset(tmpentry.entry+4, 0, 12);
    strncpy(tmpentry.entry+4, name, 12);
  }
  tmpentry.entry[0] = 0x33;
  tmpentry.entry[1] = 0x00;
  vmsfs_timestamp_to_bcd(tmpentry.entry+0x10, tstamp);
  tmpentry.entry[0x18] = blkcnt&0xff;
  tmpentry.entry[0x19] = (blkcnt>>8)&0xff;
  memset(tmpentry.entry+0x1a, 0, 6);
  if(freecnt < blkcnt)
    return 0;
  while(blkcnt--) {
    int n, blkfill = super->info->blocksz;
    unsigned char *blkptr = tmpblk;
    while(blkfill > 0) {
      if(headersize) {
	n = (headersize > blkfill? blkfill : headersize);
	memcpy(blkptr, header_ptr, n);
	blkptr += n;
	header_ptr += n;
	headersize -= n;
	blkfill -= n;
      } else if(iconsize) {
	n = (iconsize > blkfill? blkfill : iconsize);
	memcpy(blkptr, icon_ptr, n);
	blkptr += n;
	icon_ptr += n;
	iconsize -= n;
	blkfill -= n;
      } else if(eyesize) {
	n = (eyesize > blkfill? blkfill : eyesize);
	memcpy(blkptr, eye_ptr, n);
	blkptr += n;
	eye_ptr += n;
	eyesize -= n;
	blkfill -= n;
      } else if(datasize) {
	n = (datasize > blkfill? blkfill : datasize);
	memcpy(blkptr, data_ptr, n);
	blkptr += n;
	data_ptr += n;
	datasize -= n;
	blkfill -= n;
      } else if(padsize) {
	n = (padsize > blkfill? blkfill : padsize);
	memset(blkptr, 0, n);
	blkptr += n;
	padsize -= n;
	blkfill -= n;	
      } else
	return 0;
    }
    lastblk = currblk;
    if(nextblk == 0xfffa || nextblk == 0xfffc) {
      if((currblk = vmsfs_find_free_block(super)) == 0xfffc)
	return 0;
      else
	nextblk = 0xfffc;
    } else {
      currblk = nextblk;
      nextblk = vmsfs_get_fat(super, currblk);
    }
    vmsfs_set_fat(super, currblk, 0xfffa);    
    if(lastblk == 0xfffa || lastblk == 0xfffc) {
      tmpentry.entry[2] = currblk&0xff;
      tmpentry.entry[3] = (currblk>>8)&0xff;
    } else
      vmsfs_set_fat(super, lastblk, currblk);
    if(!vmsfs_write_block(super->info, currblk, tmpblk))
      return 0;
  }
  while(nextblk != 0xfffa && nextblk != 0xfffc) {
    currblk = nextblk;
    nextblk = vmsfs_get_fat(super, currblk);
    vmsfs_set_fat(super, currblk, 0xfffc);
  }
  if(headersize || iconsize || eyesize || datasize || padsize)
    return 0;
  return vmsfs_sync_superblock(super) && vmsfs_write_dir_entry(&tmpentry);
}
