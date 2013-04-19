#include <stdarg.h>

#include "cdfs.h"
#include "maple.h"
#include "vmsfs.h"

extern int state;
extern void printat(int x, int y, char *fmt, ...);

void draw_vmglyph(int xpos, int ypos, int icon, int fg, int bg)
{
  int x, y;

  unsigned int *icondata =
    (unsigned int *)(get_font_address()+0x7ef30+128*(icon+5));
  
  unsigned short *pixel = ((unsigned short *)0xa5000000)+640*ypos+xpos;
  for(y=0; y<32; y++) {
    unsigned int mask = *icondata++;
    unsigned short *pline = pixel;
    for(x=0; x<32; x++)
      *pline++ = ((mask&(1<<(x^7)))? fg : bg);
    pixel += 640;
  }
}

static char halftone[8][8];

void make_halftones(int d, int u, int x, int y, int sz)
{
  if(sz == 2) {
    halftone[x  ][y  ] = u;
    halftone[x+1][y  ] = (d<<1)+u;
    halftone[x  ][y+1] = (d<<1)+d+u;
    halftone[x+1][y+1] = d + u;
  } else {
    make_halftones((d<<2), u, x, y, sz>>1);
    make_halftones((d<<2), (d<<1)+u, x+(sz>>1), y, sz>>1);
    make_halftones((d<<2), (d<<1)+d+u, x, y+(sz>>1), sz>>1);
    make_halftones((d<<2), d+u, x+(sz>>1), y+(sz>>1), sz>>1);
  }
}

void fillrect(int x, int y, int w, int h, int r, int g, int b)
{
  unsigned short *pixel = ((unsigned short *)0xa5000000)+640*y+x;
  r = r*0x7c0/255;
  g = g*0xfc0/255;
  b = b*0x7c0/255;
  while(h--) {
    unsigned short *pline = pixel;
    int n;
    for(n=0; n<w; n++) {
      int ht = halftone[(x+n)&7][y&7];
      *pline++ =
	(((r + ht)>>6)<<11)|
	(((g + ht)>>6)<<5)|
	((b + ht)>>6);
    }
    pixel += 640;
    y++;
  }
}

int check_for_vm(int port, int unit)
{
  static short timeout[4][5];
  char *res;

  if(port<0 || port>3 || unit<0 || unit>4)
    return 0;

  do
    res = maple_docmd(port, unit+1, MAPLE_COMMAND_DEVINFO, 0, 0);
  while(*res == MAPLE_RESPONSE_AGAIN);

  if(*res == MAPLE_RESPONSE_DEVINFO && res[3]>=28) {
    struct maple_devinfo *di = (struct maple_devinfo *)(res+4);
    if(read_belong(&di->func) & MAPLE_FUNC_MEMCARD) {
      timeout[port][unit] = 10;
      return 1;
    }
  }

  if(--timeout[port][unit] < 0) {
    timeout[port][unit] = 0;
    return 0;
  }

  /* Hang on a litte longer... */
  return 1;
}

static struct vmsinfo info;
static struct superblock super;
static struct vms_file file;
static int sel_port, sel_unit;

int vmload(int loc)
{
  unsigned char *ptr = (unsigned char *)0x8c010000;

  if(!vmsfs_read_block(&info, loc, ptr) ||
     strncmp(ptr+0x30, "VMULoad", 7))
    return 0;

  while((loc = vmsfs_get_fat(&super, loc))!=0xfffc && loc!=0xfffa) {
    if(!vmsfs_read_block(&info, loc, ptr))
      return 0;
    ptr += info.blocksz;
  }

  launch(0xac010000);

  return 1;
}

#define ARR_ERASE 1
#define ARR_DRAW  2
#define ARR_MOVE  (ARR_ERASE|ARR_DRAW)

void move_selection(int port, int unit, int mode)
{
  if(mode & ARR_ERASE)
    fillrect(sel_port*96+72, sel_unit*72+48, 24, 24, 0, 0, 0);
  sel_port = port;
  sel_unit = unit;
  if(mode & ARR_DRAW)
    printat(port*8+6, unit*3+2, "%z", 6);
}

static union {
  unsigned char bytes[8192];
  unsigned int ints[2048];
} _icondata;
#define icondata (_icondata.bytes)

void draw_gfx(int xpos, int ypos, unsigned char *data, unsigned char *paldata,
	      int w, int h, int mode)
{
  unsigned short *pixel = ((unsigned short *)0xa5000000)+640*ypos+xpos;
  unsigned short pal[256];
  int x, y;
  if(mode == 2 || mode == 3) {
    y = (mode==2? 256:16);
    if(!paldata) {
      paldata = data;
      data = 0;
    }
    for(x=0; x<y; x++) {
      unsigned short p = *paldata++;
      p |= (*paldata++)<<8;
      pal[x] = ((p&0x0f00)<<4)|(p&0x0800)|
	((p&0x00f0)<<3)|((p&0x00c0)>>1)|
	((p&0x000f)<<1)|((p&0x0008)>>3);
    }
  }
  if(!data)
    data = paldata;
  switch(mode) {
  case 1:
    /* True colour */
    for(y=0; y<h; y++) {
      for(x=0; x<w; x++) {
	unsigned short p = *data++;
	p |= (*data++)<<8;
	*pixel++ = ((p&0x0f00)<<4)|(p&0x0800)|
	  ((p&0x00f0)<<3)|((p&0x00c0)>>1)|
	  ((p&0x000f)<<1)|((p&0x0008)>>3);
      }
      pixel += 640-w;
    }
    break;
  case 2:
    /* 256 colour */
    for(y=0; y<h; y++) {
      for(x=0; x<w; x++)
	*pixel++ = pal[*data++];
      pixel += 640-w;
    }
    break;
  case 3:
    /* 256 colour */
    for(y=0; y<h; y++) {
      for(x=0; x<w; x+=2) {
	int n = *data++;
	*pixel++ = pal[n>>4];
	*pixel++ = pal[n&15];
      }
      pixel += 640-w;
    }
    break;
  }
}

void draw_icondata(int xpos, int ypos, int do_colour, int fg, int bg)
{
  int mono = *(unsigned int *)(icondata+0x10);
  int colour = *(unsigned int *)(icondata+0x14);

  if(do_colour && colour)
    draw_gfx(xpos, ypos, icondata + colour, 0, 32, 32, 3);
  else {
    unsigned short *pixel = ((unsigned short *)0xa5000000)+640*ypos+xpos;
    unsigned char *id = icondata + mono;
    int x, y;
    for(y=0; y<32; y++) {
      unsigned short *pline = pixel;
      unsigned int mask = *id++;
      mask |= (*id++)<<8;
      mask |= (*id++)<<16;
      mask |= (*id++)<<24;
      for(x=0; x<32; x++)
	*pline++ = ((mask&(1<<(x^7)))? fg : bg);
      pixel += 640;
    }
  }
}

int load_icondata(struct superblock *super)
{
  static struct dir_iterator iter;
  static struct dir_entry entry;
  vmsfs_open_dir(super, &iter);
  while(vmsfs_next_named_dir_entry(&iter, &entry, "ICONDATA_VMS"))
    if(entry.entry[0] == 0x33) {
      int loc = entry.entry[2]|(entry.entry[3]<<8);
      int blks = entry.entry[0x18]|(entry.entry[0x19]<<8);
      int data = 0;

      while(data < 4096 && blks && loc != 0xfffa && loc != 0xfffc)
	if(!vmsfs_read_block(super->info, loc, icondata+data))
	  break;
	else {
	  data += super->info->blocksz;
	  loc = vmsfs_get_fat(super, loc);
	  --blks;
	}
      if(data>0x18) {
	int mono = *(unsigned int *)(icondata+0x10);
	int colour = *(unsigned int *)(icondata+0x14);
	if(data >= mono+128 &&
	   (colour == 0 || data >= colour+(512+32)))
	  return 1;
      }
    }
  return 0;
}

int attach_vm(int port, int unit, int x, int y)
{
  int exist = 1;

  if(x == 0 && y == 0) {
    x = port*96+110;
    y = unit*72+40;
  }
  fillrect(x-8, y-8, 48, 64, 240, 240, 240);
  draw_vmglyph(x, y, -4, 0x0000, 0xffe0);
  usleep(20000);
  if(vmsfs_check_unit(6*port+unit+1, 0, &info)) {
    exist++;
    draw_vmglyph(x, y, -3, 0x0000, 0xffe0);
    if(vmsfs_get_superblock(&info, &super)) {
      if(super.root[0x10])
	fillrect(x-8, y-8, 48, 64,
		 super.root[0x13], super.root[0x12], super.root[0x11]);
      draw_vmglyph(x, y, -2, 0x0000, 0xffe0);
      if(load_icondata(&super))
	draw_icondata(x, y, 1, 0x0000, 0xffe0);
      else
	draw_vmglyph(x, y, info.icon_shape, 0x0000, 0xffe0);
      exist++;
    }
  }
  if(exist<3)
    draw_vmglyph(x, y, -5, 0xf800, 0xffe0);
  return exist;
}

void detach_vm(int port, int unit)
{
  int x = port*96+110, y = unit*72+40;
  fillrect(x-8, y-8, 48, 64, 0, 0, 0);
}

int find_unit(char *exist, int pref)
{
  int i, r=-1;
  if(exist[pref])
    return pref;
  for(i=0; i<5; i++) {
    if(exist[i])
      r = i;
    if(i>=pref && r>=0)
      return r;
  }
  return r;
}

void file_inspect(struct dir_entry *e)
{
  unsigned char *ptr = (unsigned char *)0x8c010000;
  unsigned char *icons, *eyecatch;
  int loc = e->entry[2]|(e->entry[3]<<8),
    skip = e->entry[0x1a]|(e->entry[0x1b]<<8);
  int loaded = 0;
  int header_size = 128, ec_type, num_icons;
  int bk, vml=0;
  int icon_curr=-1, icon_delay=0;
  int i, j;
  struct mapledev *dev;
  char buf[36];
  static char *weekday[] = { "Monday", "Tuesday", "Wednesday", "Thursday",
			     "Friday", "Saturday", "Sunday" };
  if(e->entry[0] == 0x33) skip=0;
  while(skip--)
    if(loc == 0xfffc || loc == 0xfffa)
      break;
    else
      loc = vmsfs_get_fat(&super, loc);

  do {
    if(loc == 0xfffc || loc == 0xfffa ||
       !vmsfs_read_block(&info, loc, ptr+loaded))
      break;
    loc = vmsfs_get_fat(&super, loc);
  } while((loaded += info.blocksz)<header_size);

  clrscr(0);

  memcpy(buf, e->entry+4, 12);
  buf[12]='\0';
  printat(0, 0, "%s", buf);
  printat(16, 0, "%s", e->entry[0] == 0x33?
	  "(DATA)" : (e->entry[0] == 0xcc? "(GAME)" : "(????)"));
  printat(0, 1, "Copy protected: %s", (e->entry[1] == 0xff? "Yes":
				       (e->entry[1] == 0x00? "No" : "???")));
  printat(0, 2, "File start and size: %d + %d", *(short *)(e->entry+2),
	  *(short *)(e->entry+0x18));
  printat(0, 3, "Header offset: %d", *(short *)(e->entry+0x1a));
  printat(0, 4, "File created: %s %x%x%x%x %x:%x:%x",
	  (e->entry[0x17]>6? "???" : weekday[e->entry[0x17]]),
	  e->entry[0x10], e->entry[0x11], e->entry[0x12], e->entry[0x13],
	  e->entry[0x14], e->entry[0x15], e->entry[0x16]);

  if(loaded >= 16) {
    memcpy(buf, ptr+0x00, 16);
    buf[16] = 0;
    printat(0, 5, "Short name: %S", buf);
  }
  if(loaded >= 48) {
    memcpy(buf, ptr+0x10, 32);
    buf[32] = 0;
    printat(0, 6, "Long name: %S", buf);
  }
  if(loaded >= 64) {
    memcpy(buf, ptr+0x30, 16);
    buf[16] = 0;
    printat(0, 7, "Identifier: %S", buf);
    if(!strncmp(ptr+0x30, "VMULoad", 7)) {
      printat(0, 16, "Press %z or Enter to boot", 0x14);
      vml++;
    }
  }
  if(loaded >= 66)
    printat(0, 8, "Number of icons: %d", num_icons = *(short *)(ptr+0x40));
  else
    num_icons = 0;
  if(loaded >= 68)
    printat(0, 9, "Icon animation speed: %d", *(short *)(ptr+0x42));
  if(loaded >= 70)
    printat(0, 10, "Eyecatch type: %d", ec_type = *(short *)(ptr+0x44));
  else
    ec_type = 0;
  if(loaded >= 72)
    printat(0, 11, "CRC: %x%x", ptr[47], ptr[46]);
  if(loaded >= 76)
    printat(0, 12, "Size in bytes: %d", *(int *)(ptr+0x48));

  header_size = 128+512*num_icons;
  switch(ec_type) {
  case 1: header_size += 8064;
  case 2: header_size += 4544;
  case 3: header_size += 2048;
  }

  if(header_size > loaded)
    do {
      if(loc == 0xfffc || loc == 0xfffa ||
	 !vmsfs_read_block(&info, loc, ptr+loaded))
	break;
      loc = vmsfs_get_fat(&super, loc);
    } while((loaded += info.blocksz)<header_size);

  if(loaded < header_size)
    printat(0, 14, "Incomplete header!");

  icons = ptr + 0x80;
  eyecatch = icons + num_icons * 512;

  if(num_icons)
    draw_gfx(400, 250, icons, icons-32, 32, 32, 3);

  if(ec_type)
    draw_gfx(400, 300, eyecatch, 0, 72, 56, ec_type);

  printat(0, 17, "Press %z or ESC to return", 0xc);

  bk = 3;
  for(;;) {
    cable_monitor();
    usleep(20000);
    if(num_icons > 1 && --icon_delay<0) {
      if(++icon_curr >= num_icons)
	icon_curr = 0;
      draw_gfx(400, 250, icons+icon_curr*512, icons-32, 32, 32, 3);
      icon_delay = *(short *)(ptr+0x42);
    }
    if(!check_for_vm(info.port, info.dev-1))
      return;
    if(dev = check_pads())
      for(i=0; i<4; i++)
	switch(dev[i].func) {
	case MAPLE_FUNC_CONTROLLER:
	  if(!(dev[i].cond.controller.buttons & 2))
	    return;
	  else if(vml && !(dev[i].cond.controller.buttons & 8))
	    bk &= ~1;
	  break;
	case MAPLE_FUNC_KEYBOARD:
	  for(j=0; j<6; j++)
	    if(dev[i].cond.kbd.key[j] == 0x29)
	      return;
	    else if(vml && dev[i].cond.kbd.key[j] == 0x28)
	      bk &= ~1;
	  break;
	}
    if(bk == 2)
      bk = 3;
    else if(bk == 0) {
      vmload(*(short *)(e->entry+2));
      bk = 3;
    } else
      bk = 1;
  }
}

#define FILES_PER_PAGE 16

void dofileop(int port, int unit)
{
  static struct dir_iterator iter;
  static struct dir_entry entry[FILES_PER_PAGE+1];
  int fbase = 0, cnt, more;
  int i, j, ac, sel, last=0, acnt=0;
  struct mapledev *dev;

  for(;;) {
    clrscr(0);
    vmsfs_open_dir(&super, &iter);
    for(i=0; i<fbase;)
      if(!vmsfs_next_dir_entry(&iter, &entry[0])) {
	i = -1;
	break;
      } else if(entry[0].entry[0])
	i++;
    if(i>=0) {
      for(cnt=0; cnt<FILES_PER_PAGE; )
	if(!vmsfs_next_dir_entry(&iter, &entry[cnt]))
	  break;
	else if(entry[cnt].entry[0]) {
	  char buf[16];
	  memcpy(buf, entry[cnt].entry+4, 12);
	  buf[12]='\0';
	  printat(4, cnt, "%d", 1+cnt+fbase);
	  printat(8, cnt, "%s", buf);
	  printat(22, cnt, "%s", entry[cnt].entry[0] == 0x33?
		  "DATA" : (entry[cnt].entry[0] == 0xcc? "GAME" : "????"));
	  printat(28, cnt, "(%d blocks)",
		  entry[cnt].entry[0x18]|(entry[cnt].entry[0x19]<<8));
	  cnt++;
	}
    } else
      cnt = 0;
    more = 0;
    if(cnt>=FILES_PER_PAGE)
      while(vmsfs_next_dir_entry(&iter, &entry[FILES_PER_PAGE]))
	if(entry[FILES_PER_PAGE].entry[0]) {
	  more = 1;
	  break;
	}
    i = 0;
    if(fbase) {
      printat(0, 17, "%z or Page Up for previous", 0x12);
      i = 28;
    }
    if(more)
      printat(i, 17, "%z or Page Down for next", 0x13);
    printat(0, 18, "%z or Enter to select, %z or ESC to return", 0x0b, 0xc);
    sel = 0;
    if(cnt)
      printat(0, 0, "%z", 6);

    do {
      cable_monitor();
      usleep(20000);
      if(!check_for_vm(port, unit))
	return;
      ac = 0;
      if(dev = check_pads())
	for(i=0; i<4; i++)
	  switch(dev[i].func) {
	   case MAPLE_FUNC_CONTROLLER:
	     if(!(dev[i].cond.controller.buttons & 2))
	       ac = -4;
	     else if(cnt && !(dev[i].cond.controller.buttons & 4))
	       ac = -3;
	     else if(fbase && dev[i].cond.controller.ltrigger > 64)
	       ac = -1;
	     else if(more && dev[i].cond.controller.rtrigger > 64)
	       ac = -2;
	     else if(sel && !(dev[i].cond.controller.buttons & 16))
	       ac = 1;
	     else if(sel+1<cnt && !(dev[i].cond.controller.buttons & 32))
	       ac = 2;
	     break;
	   case MAPLE_FUNC_KEYBOARD:
	     for(j=0; j<6; j++)
	       if(dev[i].cond.kbd.key[j] == 0x29)
		 ac = -4;
	       else if(cnt && dev[i].cond.kbd.key[j] == 0x28)
		 ac = -3;
	       else if(fbase && dev[i].cond.kbd.key[j] == 0x4b)
		 ac = -1;
	       else if(more && dev[i].cond.kbd.key[j] == 0x4e)
		 ac = -2;
	       else if(sel && dev[i].cond.kbd.key[j] == 0x52)
		 ac = 1;
	       else if(sel+1<cnt && dev[i].cond.kbd.key[j] == 0x51)
		 ac = 2;
	     break;
	  }

      if(ac != last) {
	last = ac;
	acnt = 0;
      } else if(last==-4 || ++acnt<8)
	ac = 0;
      else
	acnt = 0;

      switch(ac) {
       case 1:
	 fillrect(0, sel*24, 24, 24, 0, 0, 0);
	 --sel;
	 printat(0, sel, "%z", 6);
	 break;
       case 2:
	 fillrect(0, sel*24, 24, 24, 0, 0, 0);
	 sel++;
	 printat(0, sel, "%z", 6);
	 break;
       case -1:
	 fbase -= FILES_PER_PAGE;
	 break;
       case -2:
	 fbase += FILES_PER_PAGE;
	 break;
       case -3:
	 file_inspect(&entry[sel]);
	 /* vmload(entry[sel].entry[2]|(entry[sel].entry[3]<<8)); */
	 last = -4;
	 break;
      }
    } while(ac>=0);
    if(ac==-4)
      break;
  }
}

void dovmop(int port, int unit)
{
  struct mapledev *dev;
  int i, j, al, bk;

  for(;;) {
    clrscr(0);
    printat(0, 0, "VM: %c%c", port+'A', unit+'1');
    switch((al=attach_vm(port, unit, 12, 32))) {
     case 3:
       for(i=j=0; i<info.num_blocks; i++)
	 if(vmsfs_get_fat(&super, i)==0xfffc)
	   j++;
       printat(0, 12, "Free user blocks: %d (%d%%)", j,
	       (info.num_blocks? 100*j/info.num_blocks : 0));
       printat(0, 17, "%z or F1 for file operations", 0x0f);
     case 2:
       printat(0, 4, "# of partitions: %d", info.partitions);
       printat(0, 5, "Block size: %d", info.blocksz);
       printat(0, 6, "Read/Write count: %d/%d", info.readcnt, info.writecnt);
       printat(0, 7, "Removable media: %s", (info.removable? "Yes":"No"));
       printat(0, 8, "Root block: %d", info.root_loc);
       printat(0, 9, "FAT start and size: %d + %d", info.fat_loc, info.fat_size);
       printat(0, 10, "Dir start and size: %d + %d", info.dir_loc, info.dir_size);
       printat(0, 11, "Number of user blocks: %d", info.num_blocks);
    }
    
    printat(0, 18, "Press %z or ESC to return to VM screen", 0xc);
    bk = 7;
    do {
      cable_monitor();
      usleep(20000);
      if(!check_for_vm(port, unit))
	return;
      if(dev = check_pads())
	for(i=0; i<4; i++)
	  switch(dev[i].func) {
	   case MAPLE_FUNC_CONTROLLER:
	     if(!(dev[i].cond.controller.buttons & 2))
	       bk &= ~1;
	     else if(al>=3 && !(dev[i].cond.controller.buttons & 1024)) {
	       dofileop(port, unit);
	       bk = 0;
	     } break;
	   case MAPLE_FUNC_KEYBOARD:
	     for(j=0; j<6; j++)
	       if(dev[i].cond.kbd.key[j] == 0x29)
		 bk &= ~1;
	       else if(al>=3 && dev[i].cond.kbd.key[j] == 0x3a) {
		 dofileop(port, unit);
		 bk = 0;
	       }
	     break;
	  }
      if(bk == 6)
	bk = 7;
      else if(bk == 4)
	return;
      else if(bk)
	bk = 5;
    } while(bk);
  }
}

void vmoper()
{
  int port, unit;
  char exist[4][5];
  int nvm, lasta, acnt;

  state = -1;
  clrscr(0);
  make_halftones(1, 0, 0, 0, 8);

  for(;;) {
    clrscr(0);
    memset(exist, 0, sizeof(exist));
    nvm = 0;
    lasta = -2;
    acnt = 0;

    for(unit=0; unit<5; unit++)
      printat(2, unit*3+2, "%c", unit+'1');
    for(port=0; port<4; port++) {
      printat(port*8+10, 0, "%c", port+'A');
      for(unit=0; unit<5; unit++)
	if(check_for_vm(port, unit)) {
	  exist[port][unit] = attach_vm(port, unit, 0, 0);
	  if(!nvm++)
	    move_selection(port, unit, ARR_DRAW);
	}
    }
    if(nvm)
      printat(0, 17, "Select a VM with %z or Enter", 0xb);

    printat(0, 18, "Press %z or ESC to return to main screen", 0xc);

    for(;;) {
      struct mapledev *dev;
      int i, j, a;

      cable_monitor();
      a = 0;
      if(dev = check_pads())
	for(i=0; !a && i<4; i++)
	  switch(dev[i].func) {
	   case MAPLE_FUNC_CONTROLLER:
	     j = dev[i].cond.controller.buttons;
	     if(!(j & 4)) {
	       dovmop(sel_port, sel_unit);
	       a=-1;
	     } else if(!(j & 2))
	       a=-2;
	     else if(!(j & 16))
	       a=1;
	     else if(!(j & 32))
	       a=2;
	     else if(!(j & 64))
	       a=3;
	     else if(!(j & 128))
	       a=4;
	     break;
	   case MAPLE_FUNC_KEYBOARD:
	     for(j=0; !a && j<6; j++)
	       switch(dev[i].cond.kbd.key[j]) {
		case 0x29:
		  a=-2;
		  break;
		case 0x28:
		  dovmop(sel_port, sel_unit);
		  a=-1;
		  break;
		case 0x52:
		  a=1;
		  break;
		case 0x51:
		  a=2;
		  break;
		case 0x50:
		  a=3;
		  break;
		case 0x4f:
		  a=4;
		  break;
	       }
	     break;
	  }
      if(a==-2)
	if(lasta == -2)
	  continue;
	else
	  return;
      if(a<0)
	break;
      if(a != lasta) {
	lasta = a;
	acnt = 0;
      } else if(++acnt<5)
	a = 0;
      else
	acnt = 0;
      if(nvm) {
	switch(a) {
	 case 1:
	   for(unit=sel_unit; --unit>=0; )
	     if(exist[sel_port][unit]) {
	       move_selection(sel_port, unit, ARR_MOVE);
	       break;
	     }
	   break;
	 case 2:
	   for(unit=sel_unit; ++unit<5; )
	     if(exist[sel_port][unit]) {
	       move_selection(sel_port, unit, ARR_MOVE);
	       break;
	     }
	   break;
	 case 3:
	   for(port=sel_port; --port>=0;)
	     if((unit = find_unit(exist[port], sel_unit))>=0) {
	       move_selection(port, unit, ARR_MOVE);
	       break;
	     }
	   break;
	 case 4:
	   for(port=sel_port; ++port<4;)
	     if((unit = find_unit(exist[port], sel_unit))>=0) {
	       move_selection(port, unit, ARR_MOVE);
	       break;
	     }
	   break;
	}
      }

      usleep(20000);
      for(port=0; port<4; port++)
	for(unit=0; unit<5; unit++) {
	  int u = check_for_vm(port, unit);
	  if(u && !exist[port][unit]) {
	    exist[port][unit] = attach_vm(port, unit, 0, 0);
	    if(!nvm++) {
	      printat(0, 17, "Select a VM with %z or Enter", 0xb);
	      move_selection(port, unit, ARR_DRAW);
	    }
	  } else if(!u && exist[port][unit]) {
	    detach_vm(port, unit);
	    exist[port][unit] = 0;
	    if(!--nvm) {
	      fillrect(0, 17*24, 640, 24, 0, 0, 0);
	      move_selection(0, 0, ARR_ERASE);
	    }
	  }
	}

      if(nvm && !exist[sel_port][sel_unit])
	for(port=0; port<4; port++)
	  for(unit=0; unit<5; unit++)
	    if(exist[port][unit]) {
	      move_selection(port, unit, ARR_MOVE);
	      port=4;
	      break;
	    }
    }
  }
}
