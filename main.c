#include <stdarg.h>

#include "cdfs.h"
#include "maple.h"

extern unsigned char pvrdata[];
extern unsigned int pvrdatasize;

#define ipdata ((unsigned char *)0x8c008000)

static unsigned char selected_sysid[8];

void atexit() { }

#define TMU_REG(n) ((volatile void *)(0xffd80000+(n)))
#define TOCR (*(volatile unsigned char *)TMU_REG(0))
#define TSTR (*(volatile unsigned char *)TMU_REG(4))
#define TCOR0 (*(volatile unsigned int *)TMU_REG(8))
#define TCNT0 (*(volatile unsigned int *)TMU_REG(12))
#define TCR0 (*(volatile unsigned short *)TMU_REG(16))

#define USEC_TO_TIMER(x) (((x)*100)>>11)

static void init_tmr0()
{
  TSTR = 0;
  TOCR = 0;
  TCOR0 = ~0;
  TCNT0 = ~0;
  TCR0 = 4;
  TSTR = 1;
}

static unsigned long Timer( )
{
  return ~TCNT0;
}

void usleep(unsigned int usec)
{
  unsigned int t0 = Timer();
  unsigned int dly = USEC_TO_TIMER(usec);
  while( ((unsigned int)(Timer()-t0)) < dly );
}

void cable_monitor()
{
  static int cab = -1;
  int i;

  if((i = check_cable()) != cab)
    init_video((cab = i), 1);
}

char *itoa(int x) 
{ 
  static char buf[30];
  int minus = 0;
  int ptr=29;
  buf[29]=0;

  if(!x) return "0";
  if( x < 0 )  {  minus=1;  x = -x; }
  while(x > 0) 
  { 
    buf[--ptr] = x%10 + '0'; 
    x/=10; 
  }
  if( minus ) buf[--ptr] = '-';
  return buf+ptr; 
}

void chrat(int x, int y, char c, int color)
{
  char buf[2];
  buf[0] = c;
  buf[1] = '\0';
  draw_string(x*12, y*24, buf, color);
}

void wideat(int x, int y, int c, int color)
{
  draw_char24(x*12, y*24, c, color);
}

int strat(int x, int y, char *s, int color)
{
  draw_string(x*12, y*24, s, color);
  return strlen(s);
}

int wstrat(int x, int y, char *s, int color)
{
  int x0=x, c;
  while((c=0xff&*s++))
    if(c<0x81) {
      if(c<0x21 || c>0x7e)
	c = 96;
      else if(c==0x5c)
	c = 95;
      else if(c==0x7e)
	c = 0;
      else
	c -= 32;
      draw_char12(x*12, y*24, c, color);
      x++;
    } else if(c>=0xa1 && c<=0xdf) {
      draw_char12(x*12, y*24, c+32, color);
      x++;
    } else {
      int row, col;
      if(!*s) break;
      c = (c<<8) | (0xff&*s++);
      if(c >= 0xa000) c -= 0x4000;
      if((c & 0xff) > 0x9e) {
	row = 2*(c>>8)-0x101;
	col = (c & 0xff)-0x9f;
      } else {
	row = 2*(c>>8)-0x102;
	col = (c & 0xff)-0x40;
	if(col >= 0x40) --col;
      }
      if(row<0 || col<0 || row>83 || col>93 || (row>6 && row<15))
	row = col = 0;
      else if(row>6)
	row -= 8;
      draw_char24(x*12, y*24, row*94+col, color);
      x += 2;
    }
  return x-x0;
}

void printat(int x, int y, char *fmt, ...)
{
  int p;
  int color = 0xffff;
  va_list va;
  va_start(va, fmt);
  while((p = *fmt++))
    if(p=='%')
      switch(*fmt++) 
      {
       case '\0': --fmt;    break;

       case 's': x += strat(x, y, va_arg(va, char *), color );   break;
       case 'S': x += wstrat(x, y, va_arg(va, char *), color );   break;
       case 'c': chrat(x++, y, va_arg(va, int), color );   break;
       case 'W': color = 0xffff; break;
       case 'C': color = va_arg(va, int); break;
       case 'z': wideat(x, y, va_arg(va, int)+7056, 0xffe0); x+=2; break;
       case '%': chrat(x++, y, '%', color); break;
       case 'd': x += strat(x, y, itoa(va_arg(va, int)), color ); break;
       case 'p': x += strat(x, y, "(void *)0x", color);
       case 'x': 
       {
         char buf[9];
         int i, d;
         int n = va_arg( va, int );
         for(i=0; i<8; i++) 
         {
           d = n&15;
           n>>=4;
           if(d<10)
             buf[7-i]='0'+d;
           else
             buf[7-i]='a'+(d-10);
         }
         buf[8]=0;
	 if(fmt[-1]=='p')
	   x += strat(x, y, buf, color);
	 else
	   x += strat(x, y, buf+6, color);
         break;
       }
       case 'b':
       {
         char bits[33];
         int i, d = va_arg( va, int);
         bits[32]=0;
         for( i = 0; i<31; i++ )
           if( d & (1<<i) )
             bits[31-i] = '1';
           else
             bits[31-i] = '0';
         x += strat(x, y, bits, color );
	 break;
       }
      }
    else
      chrat(x++, y, p, color);
  va_end(va);
}


#define MODE_ARGB1555 0x00
#define MODE_RGB565   0x01
#define MODE_ARGB4444 0x02
#define MODE_YUV422   0x03
#define MODE_BUMPMAP  0x04
#define MODE_RGB555   0x05
#define MODE_ARGB8888 0x06

#define MODE_TWIDDLE            0x0100
#define MODE_TWIDDLE_MIPMAP     0x0200
#define MODE_COMPRESSED         0x0300
#define MODE_COMPRESSED_MIPMAP  0x0400
#define MODE_CLUT4              0x0500
#define MODE_CLUT4_MIPMAP       0x0600
#define MODE_CLUT8              0x0700
#define MODE_CLUT8_MIPMAP       0x0800
#define MODE_RECTANGLE          0x0900
#define MODE_STRIDE             0x0b00
#define MODE_TWIDDLED_RECTANGLE 0x0d00

static int twiddletab[1024];

static void init_twiddletab()
{
  int x;
  for(x=0; x<1024; x++)
    twiddletab[x] = (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)|
      ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9);
}

static void pvr_decode_rect(int attr, unsigned char *src, unsigned short *dst,
			    int stride, unsigned int h, unsigned int w)
{
  int x, y;
  unsigned int p;
  for(y=0; y<h; y++) {
    for(x=0; x<w; x++) {
      int dst_r, dst_g, dst_b, a = 255;
      switch(attr&0xff) {
       case MODE_ARGB1555:
	 if(!(src[1]&0x80)) { a = 0; src+=2; break; }
       case MODE_RGB555:
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
	 dst_g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
	 dst_b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 src+=2;
	 break;
       case MODE_RGB565:
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0xf800)>>8)|((p&0xe000)>>13);
	 dst_g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
	 dst_b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 src+=2;
	 break;
       case MODE_ARGB4444:
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
	 dst_g = (p&0x00f0)|((p&0x00f0)>>4);
	 dst_b = ((p&0x000f)<<4)|(p&0x000f);
	 a = ((p&0xf000)>>8)|((p&0xf000)>>12);
	 src+=2;
	 break;
      }
      if(a) {
	dst_r>>=3;
	dst_g>>=2;
	dst_b>>=3;
	if(a<255) {
	  int p = *dst;
	  dst_r = ((dst_r * a) + (((p&0xf800)>>11)*(255-a)))>>8;
	  dst_g = ((dst_g * a) + (((p&0x07e0)>>5)*(255-a)))>>8;
	  dst_b = ((dst_b * a) + ((p&0x001f)*(255-a)))>>8;	
	}
	*dst++ = (dst_r<<11)|(dst_g<<5)|dst_b;
      }
      else
	dst++;
    }
    dst += stride;
  }
}

static void pvr_decode_twiddled(int attr, unsigned char *s, unsigned short *dst,
				int stride, unsigned int sz)
{
  unsigned int x, y;
  unsigned char *src;
  unsigned int p;
  for(y=0; y<sz; y++) {
    for(x=0; x<sz; x++) {
      int dst_r, dst_g, dst_b, a = 255;
      switch(attr&0xff) {
       case MODE_ARGB1555:
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 if(!(src[1]&0x80)) { a = 0; break; }
       case MODE_RGB555:
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
	 dst_g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
	 dst_b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 break;
       case MODE_RGB565:
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0xf800)>>8)|((p&0xe000)>>13);
	 dst_g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
	 dst_b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 break;
       case MODE_ARGB4444:
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst_r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
	 dst_g = (p&0x00f0)|((p&0x00f0)>>4);
	 dst_b = ((p&0x000f)<<4)|(p&0x000f);
	 a = ((p&0xf000)>>8)|((p&0xf000)>>12);
	 break;
      }
      if(a) {
	dst_r>>=3;
	dst_g>>=2;
	dst_b>>=3;
	if(a<255) {
	  int p = *dst;
	  dst_r = ((dst_r * a) + (((p&0xf800)>>11)*(255-a)))>>8;
	  dst_g = ((dst_g * a) + (((p&0x07e0)>>5)*(255-a)))>>8;
	  dst_b = ((dst_b * a) + ((p&0x001f)*(255-a)))>>8;	
	}
	*dst++ = (dst_r<<11)|(dst_g<<5)|dst_b;
      }
      else
	dst++;
    }
    dst += stride;
  }
}
 
int draw_gdtex(unsigned char *s, unsigned long len)
{
   int attr;
   unsigned int h, w, x;
   unsigned short *img = ((unsigned short *)(void*)0xa5000000)+640-256+(480-256)*640;

   if(len >= 12 && !strncmp(s, "GBIX", 4)) {
     int l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
     if(l>=4 && l<=len-8) {
       len -= l+8;
       s += l+8;
     }
   }

   if(len < 16 || strncmp(s, "PVRT", 4))
     return -1;
   else {
     int l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
     if(l+8>len)
       return -1;
     else if(l<8)
       return -1;
     len = l+8;
   }

   attr = s[8]|(s[9]<<8)|(s[10]<<16)|(s[11]<<24);
   w = s[12]|(s[13]<<8);
   h = s[14]|(s[15]<<8);

   if(w != 256 || h != 256)
     return -1;

   s += 16;
   len -= 16;

   {
     int twiddle=0, hasalpha=0, bpp=0;
     int mipmap=0;

     switch(attr&0xff00) {
      case MODE_TWIDDLE_MIPMAP:
	mipmap = 1;
      case MODE_TWIDDLE:
	twiddle = 1;
	if(w != h || w<8 || w>1024 || (w&(w-1)))
	  return -1;
      case MODE_RECTANGLE:
      case MODE_STRIDE:
	break;
      case MODE_TWIDDLED_RECTANGLE:
	twiddle = 1;
	if((w<h && (w<8 || w>1024 || (w&(w-1)) || h%w)) ||
	   (h>=w && (h<8 || h>1024 || (h&(h-1)) || w%h)))
	  return -1;
	break;
      case MODE_COMPRESSED:
      case MODE_COMPRESSED_MIPMAP:
	return -1;
      case MODE_CLUT4:
      case MODE_CLUT4_MIPMAP:
      case MODE_CLUT8:
      case MODE_CLUT8_MIPMAP:
   	return -1;
      default:
	return -1;
     }

     switch(attr&0xff) {
      case MODE_ARGB1555:
      case MODE_ARGB4444:
	hasalpha=1;
      case MODE_RGB565:
      case MODE_RGB555:
	bpp=2; break;
      case MODE_YUV422:
	return -1;
      case MODE_ARGB8888:
	return -1;
      case MODE_BUMPMAP:
	return -1;
      default:
	return -1;
     }

     if(mipmap) /* Just skip everything except the largest version */
       for(x=w; x>>=1;)
	 mipmap += x*x;

     if(len < (int)(bpp*(h*w+mipmap)))
       return -1;

     s += bpp*mipmap;

     if(twiddle)
       if(h<w)
	 for(x=0; x<w; x+=h)
	   pvr_decode_twiddled(attr, s+bpp*h*x, img+x, 640-h, h);
       else
	 for(x=0; x<h; x+=w)
	   pvr_decode_twiddled(attr, s+bpp*w*x, img+640*x, 640-w, w);
     else
       pvr_decode_rect(attr, s, img, 640-w, h, w);

   }
   return 0;
}



#define STATE_OPEN    0
#define STATE_NODISK  1
#define STATE_DISK    2
#define STATE_DISKTMP 3

int state, dtype, ipvalid;

int decode_crc(unsigned char *p)
{
  int i, n = 0;
  for(i=0; i<4; i++)
    if(p[i]>='0' && p[i]<='9')
      n = (n<<4) | (p[i]&15);
    else if((p[i]>='A' && p[i]<='F') || (p[i]>='a' && p[i]<='f'))
      n = (n<<4) | ((p[i]&15)+9);
    else
      return -1;
  return n;
}

int calcCRC(const unsigned char *buf, int size)
{
  int i, c, n = 0xffff;
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

int check_disc(int gdrom)
{
  int r;
  int t=1;

  ipvalid = 0;
  memset(ipdata, 0, 32768);

  if((r=load_ip(gdrom, ipdata))<0 || memcmp(ipdata, "SEGA SEGAKATANA ", 16)) {
    int f;
    t=2;
    if((f = open(gdrom, "IP.BIN", O_RDONLY))>=0) {
      read(f, ipdata, 32768);
      close(f);
    } else
      return (r<0? r : -1);
    if(memcmp(ipdata, "SEGA SEGAKATANA ", 16))
      return -1;
  }
  ipvalid = t;
  return 0;
}

int check_gdtex(int gdrom)
{
  int r, f;
  if((f = open(gdrom, "0GDTEX.PVR", O_RDONLY))<0)
    return f;
  r = read(f, pvrdata, pvrdatasize);
  close(f);
  if(r<0)
    return r;
  if(r>=pvrdatasize)
    return -1;
  return draw_gdtex(pvrdata, r);
}

int parsehex(char *ptr, int n)
{
  int res=0;
  while(n--) {
    char c = *ptr++;
    if(c>='0' && c<='9')
      res = (res<<4)|(c-'0');
    else if((c>='A' && c<='F') || (c>='a' && c<='f'))
      res = (res<<4)|((c+9)&0xf);
  }
  return res;
}

void set_state(int newstate, int newdtype)
{
  int i;
  char buf[136];
  static char *state_str[] = {
    "Lid open",
    "No disk",
    "Disk",
    "Lid closed...",
  };
  static char *dtype_str[] = {
    "CDDA",
    "CDROM",
    "CDROM/XA",
    "CDI",
    "???",
    "???",
    "???",
    "???",
    "GDROM",
    "???",
    "???",
    "???",
    "???",
    "???",
    "???",
    "???",
  };

  clrscr(0);
  printat(40, 0, "M'Boot v1.1");
  printat(0, 0, "%s", state_str[newstate]);
  printat(0, 16, "%z or F1 for VM operations", 0x0f);
  printat(0, 17, "%z or F2 for Flash info", 0x10);

  switch((state = newstate)) {
#if 0
   case STATE_OPEN:
     {
       int i;
       unsigned int param[4];
       cdfs_init();
       for(i=0; i<1000; i++) {
	 gdGdcGetDrvStat(param);
	 if(param[0] == 6)
	   break;
	 usleep(1000);
       }
     }
     break;
#endif
   case STATE_DISK:
     printat(0, 1, "%s", dtype_str[(dtype=newdtype)>>4]);
     check_disc(dtype==0x80);
     if(ipvalid) {
       int x, y=2, n;
       strncpy(buf, ipdata+0x80, 53);
       buf[53] = 0;
       printat(0, y++, "%s", buf);
       strncpy(buf, ipdata+0x80+53, 53);
       buf[53] = 0;
       if(strspn(buf, " ")!=53)
	 printat(0, y++, "%s", buf);
       strncpy(buf, ipdata+0x70, 16);
       buf[16] = 0;
       printat(0, y++, "By: %s", buf);

       printat(0, y++, "IP Checksum %s",
	       (decode_crc(ipdata+0x20) == calcCRC(ipdata+0x40, 16)?
		"valid" : "INVALID"));
       printat(0, y, "Region: ");
       x=8;
       if(ipdata[0x30]=='J') {
	 printat(x, y, "JAPAN");
	 x += 6;
       }
       if(ipdata[0x31]=='U') {
	 printat(x, y, "USA");
	 x += 4;
       }
       if(ipdata[0x32]=='E') {
	 printat(x, y, "EUROPE");
	 x += 7;
       }
       printat(0, ++y, "Require: ");
       x = 9;
       n = parsehex(ipdata+0x38, 8);
       if(n&0x1000) {
	 printat(x, y, "%z%z%z", 0x14, 0xb, 0xc);
	 x += 6;
       }
       if(n&0x2000) {
	 printat(x, y, "%z", 0xd);
	 x += 2;
       }
       if(n&0x4000) {
	 printat(x, y, "%z", 0xe);
	 x += 2;
       }
       if(n&0x8000) {
	 printat(x, y, "%z", 0xf);
	 x += 2;
       }
       if(n&0x10000) {
	 printat(x, y, "%z", 0x10);
	 x += 2;
       }
       if(n&0x20000) {
	 printat(x, y, "%z", 0x11);
	 x += 2;
       }
       if(n&0x80000) {
	 printat(x, y, "%z", 0x13);
	 x += 2;
       }
       if(n&0x100000) {
	 printat(x, y, "%z", 0x12);
	 x += 2;
       }
       if(n&0x1bf000) x++;
       if(n&0x1000) {
	 printat(x, y, "DIR");
	 x += 4;
       }
       if(n&0x40000) {
	 printat(x, y, "DIR2");
	 x += 5;
       }
       if(n&0x200000) {
	 printat(x, y, "AX");
	 x += 3;
       }
       if(n&0x400000) {
	 printat(x, y, "AY");
	 x += 3;
       }
       if(n&0x800000) {
	 printat(x, y, "AX2");
	 x += 4;
       }
       if(n&0x1000000) {
	 printat(x, y, "AY2");
	 x += 4;
       }
       printat(0, ++y, "Support: ");
       x = 9;
       if(n&0x800) {
	 printat(x, y, "%z", 0x15);
	 x += 3;
       }
       if(n&0x400) {
	 printat(x, y, "Mike");
	 x += 5;
       }
       if(n&0x200) {
	 printat(x, y, "PuruPuru");
	 x += 9;
       }
       if(n&0x200) {
	 printat(x, y, "Other");
	 x += 6;
       }
       if(n&0x10) {
	 printat(x, y, "VGA");
	 x += 4;
       }
       if(n&0x8000000) {
	 printat(x, y, "Mouse");
	 x += 6;
       }
       if(n&0x4000000) {
	 printat(x, y, "Keybd");
	 x += 6;
       }
       if(n&0x2000000) {
	 printat(x, y, "Gun");
	 x += 4;
       }
       y++;
       strncpy(buf, ipdata+0x40, 10);
       buf[10] = 0;
       printat(0, y++, "Product number: %s", buf);
       strncpy(buf, ipdata+0x4a, 6);
       buf[6] = 0;
       printat(0, y++, "Product version: %s", buf);
       strncpy(buf, ipdata+0x50, 16);
       buf[16] = 0;
       printat(0, y++, "Release date: %s", buf);
       strncpy(buf, ipdata+0x20, 16);
       buf[16] = 0;
       printat(0, y++, "Device info: %s", buf);
       strncpy(buf, ipdata+0x60, 16);
       buf[16] = 0;
       printat(0, y++, "Boot filename: %s", buf);

       if(n&1)
	 printat(9, 1, "(WinCE)");
     } else
       memset(ipdata, 0, 32768);
     check_gdtex(dtype==0x80);
     if(ipvalid)
       printat(0, 15, "%z or Enter to boot", 0x14);

     break;
  }
}


#define MAXCHUNK (2048*1024)

static unsigned int seed;

void my_srand(unsigned int n)
{
  seed = n & 0xffff;
}

unsigned int my_rand()
{
  seed = (seed * 2109 + 9273) & 0x7fff;
  return (seed + 0xc000) & 0xffff;
}

struct loadhandle {
  int fd;
  unsigned char *buffer;
  int bufpos, bufvalid;
};

#define BUFSIZE (2048*16)

void load(struct loadhandle *fd, unsigned char *ptr, unsigned long sz)
{
  if(fd->bufvalid < sz) {
    fd->bufpos = 0;
    if((fd->bufvalid = read(fd->fd, fd->buffer, BUFSIZE)) < sz)
      start();
  }
  memcpy(ptr, fd->buffer + fd->bufpos, sz);
  fd->bufpos += sz;
  fd->bufvalid -= sz;
}

void load_chunk(struct loadhandle *fd, unsigned char *ptr, unsigned long sz)
{
  static int idx[MAXCHUNK/32];
  int i;

  /* Convert chunk size to number of slices */
  sz /= 32;

  /* Initialize index table with unity,
     so that each slice gets loaded exactly once */
  for(i = 0; i < sz; i++)
    idx[i] = i;

  for(i = sz-1; i >= 0; --i)
    {
      /* Select a replacement index */
      int x = (my_rand() * i) >> 16;

      /* Swap */
      int tmp = idx[i];
      idx[i] = idx[x];
      idx[x] = tmp;

      /* Load resulting slice */
      load(fd, ptr+32*idx[i], 32);
    }
}

void load_file(int fd, unsigned char *ptr, unsigned long filesz)
{
  unsigned long chunksz;
  struct loadhandle lh;

  my_srand(filesz);

  lh.fd = fd;
  lh.buffer = pvrdata;
  lh.bufpos = lh.bufvalid = 0;

  /* Descramble 2 meg blocks for as long as possible, then
     gradually reduce the window down to 32 bytes (1 slice) */
  for(chunksz = MAXCHUNK; chunksz >= 32; chunksz >>= 1)
    while(filesz >= chunksz)
      {
	load_chunk(&lh, ptr, chunksz);
	filesz -= chunksz;
	ptr += chunksz;
      }

  /* Load final incomplete slice */
  if(filesz)
    load(&lh, ptr, filesz);
}

void waitkey(int btns, int kbdkey)
{
  struct mapledev *dev;
  int i, j;
  for(;;) {
    cable_monitor();
    if(dev = check_pads())
      for(i=0; i<4; i++)
	switch(dev[i].func) {
	 case MAPLE_FUNC_CONTROLLER:
	   if(!(dev[i].cond.controller.buttons & btns))
	     return;
	 case MAPLE_FUNC_KEYBOARD:
	   for(j=0; j<6; j++)
	     if(dev[i].cond.kbd.key[j] == kbdkey)
	       return;
	   break;
	}
  }
}

struct sysinfo {
  unsigned char id[8];
  unsigned char settings[16];
};

int syscall_info_flash(int sect, int *info)
{
  return (*(int (**)())0x8c0000b8)(sect,info,0,0);  
}

int syscall_read_flash(int offs, void *ptr, int cnt)
{
  return (*(int (**)())0x8c0000b8)(offs,ptr,cnt,1);  
}

int syscall_write_flash(int offs, void *ptr, int cnt)
{
  return (*(int (**)())0x8c0000b8)(offs,ptr,cnt,2);
}

void get_sysinfo(struct sysinfo *si)
{
  int i;

  syscall_read_flash(0x1a056, si->id, 8);
  syscall_read_flash(0x1a000, si->settings, 5);
  for(i=0; i<11; i++)
    si->settings[5+i] = 0;
}


void try_boot()
{
  char fnbuf[20];
  int l, f;
  int wince=0, invasion=0;
  struct sysinfo si;

  if(ipvalid)
    strncpy(fnbuf, ipdata+0x60, 16);
  else
    strcpy(fnbuf, "1ST_READ.BIN");
  fnbuf[16] = 0;
  l = strlen(fnbuf);
  while(l && fnbuf[l-1]==' ')
    fnbuf[--l] = '\0';
  if(!l)
    return;
  /*  printat(0,12,"Opening %s", fnbuf); */
  printat(0, 19, "Loading...");
  if((f = open(dtype==0x80, fnbuf, O_RDONLY))<0)
    return;
  l = file_length(f);
  /* printat(0,13,"Ok, size = %d", l); */

  if(ipvalid)
    wince = parsehex(ipdata+0x38, 8)&1;

  if(ipvalid==1 && dtype==0x20)
    load_file(f, (void*)0x8c010000, l);
  else {
    if(wince) {
      /* Skip header block... */
      if(read(f, (void*)0x8c010000, 2048) != 2048)
	start();
      l -= 2048;
    }
    if(read(f, (void*)0x8c010000, l) != l)
      start();
  }

  /* if ipvalid != 1, do cracking stuff */
  if(ipvalid != 1)
    invasion |= 1;

  get_sysinfo(&si);
  if(memcmp(si.id, selected_sysid, 8)) {
    /* fake sysid */
    extern void set_fake_sysid(unsigned char *);
    set_fake_sysid(selected_sysid);
    invasion |= 2;
  }
  memcpy(0x8c000068, selected_sysid, 8);

  if(invasion) {
    extern void install_crack(int);
    install_crack(invasion);
  }

  launch(ipvalid? 0xac00b800:0xac010000);
}



unsigned short flash_crc(char *buf, int cnt)
{
  static unsigned short table[] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
  };
  unsigned short tmp = ~0;
  while(cnt--)
    tmp = (tmp<<8)^table[(tmp>>8)^(unsigned char)*buf++];
  return ~tmp;
}

int flash_read_sector(int partition, int sec, char *dst)
{
  int s, r, n, b, bmb, got=0;
  int info[2];
  static char buf[64];
  static char bm[64];

  if((r = syscall_info_flash(partition, info))<0)
    return r;

  if((r = syscall_read_flash(info[0], buf, 64))<0)
    return r;

  if(memcmp(buf, "KATANA_FLASH", 12) ||
     buf[16] != partition || buf[17] != 0)
    return -2;

  n = (info[1]>>6)-1-((info[1] + 0x7fff)>>15);
  bmb = n+1;
  for(b = 0; b < n; b++) {
    if(!(b&511)) {
      if((r = syscall_read_flash(info[0] + (bmb++ << 6), bm, 64))<0)
	return r;
    }
    if(!(bm[(b>>3)&63] & (0x80>>(b&7))))
      if((r = syscall_read_flash(info[0] + ((b+1) << 6), buf, 64))<0)
	return r;
      else if((s=*(unsigned short *)(buf+0)) == sec &&
	      flash_crc(buf, 62) == *(unsigned short *)(buf+62)) {
	memcpy(dst+(s-sec)*60, buf+2, 60);
	got=1;
      }
  }

  return got;
}


int flash_write_sector(int partition, int sec, char *src)
{
  int s, r, n, b, bmb, got=0;
  int info[2];
  static char buf[64];
  static char buf2[64];
  static char bm[64];

  if((r = syscall_info_flash(partition, info))<0)
    return r;

  if((r = syscall_read_flash(info[0], buf, 64))<0)
    return r;

  if(memcmp(buf, "KATANA_FLASH", 12) ||
     buf[16] != partition || buf[17] != 0)
    return -2;

  n = (info[1]>>6)-1-((info[1] + 0x7fff)>>15);
  bmb = n+1;

  memcpy(buf+2, src, 60);
  *(unsigned short *)(buf+0) = sec;
  *(unsigned short *)(buf+62) = flash_crc(buf, 62);

  for(b = 0; b < n; b++) {
    if(!(b&511)) {
      if((r = syscall_read_flash(info[0] + (bmb++ << 6), bm, 64))<0)
	return r;
    }
    if(bm[(b>>3)&63] & (0x80>>(b&7))) {
      /* Free sector */
      bm[(b>>3)&63] &= ~(0x80>>(b&7));
      if((r = syscall_write_flash(info[0] + ((bmb-1) << 6), bm, 64))<0 ||
	 (r = syscall_write_flash(info[0] + ((b+1) << 6), buf, 64))<0 ||
	 (r = syscall_read_flash(info[0] + ((b+1) << 6), buf2, 64))<0)
	return r;
      if(!memcmp(buf, buf2, 64))
	return 1;
    }
  }
  return 0;
}

static void restore_fake_id()
{
  unsigned char sec[60];
  if(flash_read_sector(2,177,(char*)sec)==1 &&
     !memcmp(sec, "<M'Boot>", 8))
    memcpy(selected_sysid, sec+8, 8);
}


static void save_fake_id()
{
  unsigned char sec[60];
  if(flash_read_sector(2,177,(char*)sec)!=1 ||
     memcmp(sec, "<M'Boot>", 8))
    memset(sec, 0, 60);
  memcpy(sec, "<M'Boot>", 8);
  memcpy(sec+8, selected_sysid, 8);
  if(flash_write_sector(2,177,(char*)sec)!=1) {
    clrscr(0);
    printat(2, 2, "Failed to make data permanent.");
    printat(2, 4, "Possible case:  Your flashrom is fragmented.");
    printat(2, 6, "Reset your Dreamcast and change some setting in");
    printat(2, 7, "the ROM menu in order to defragment it.");
    printat(0, 17, "Press %z or ESC to proceed", 0xc);

    waitkey(2, 0x29);
  }
}


static void edit_sysid()
{
  struct mapledev *dev;
  int i, j, ac, last = -1, acnt = 0;
  int editpos = 0;
  struct sysinfo si;
  extern void fillrect(int x, int y, int w, int h, int r, int g, int b);

  state = -1;
  clrscr(0);
  get_sysinfo(&si);
  printat(0, 0, "Real System ID:");
  for(i=0; i<8; i++)
    printat(5+3*i, 2, "%x", si.id[i]);
  printat(0, 5, "Fake System ID:");
  for(i=0; i<8; i++)
    printat(5+3*i, 7, "%x", selected_sysid[i]);
  printat(0, 15, "Press %z or Delete to copy real ID to fake ID", 0x10);
  printat(0, 16, "Press %z or Enter to make this fake ID permanent", 0xb);
  printat(0, 17, "Press %z or ESC to return without making permanent", 0xc);
  
  for(;;) {
    printat(5+3*(editpos>>1)+(editpos&1), 8, "^");
    cable_monitor();
    usleep(20000);
    ac = -1;
    if(dev = check_pads())
      for(i=0; i<4; i++)
	switch(dev[i].func) {
	 case MAPLE_FUNC_CONTROLLER:
	   if(!((j=dev[i].cond.controller.buttons) & 2))
	     ac = -2;
	   else if(!(j & 16))
	     ac = -4;
	   else if(!(j & 32))
	     ac = -6;
	   else if(!(j & 64))
	     ac = -5;
	   else if(!(j & 128))
	     ac = -3;
	   else if(!(j & 0x200))
	     ac = -7;
	   else if(!(j & 4))
	     ac = -8;
	   break;
	 case MAPLE_FUNC_KEYBOARD:
	   for(j=0; j<6; j++)
	     switch(dev[i].cond.kbd.key[j]) {
	      case 0x04 ... 0x09:
	      case 0x1e ... 0x27:
		ac = (dev[i].cond.kbd.key[j] == 0x27? 0 :
		       (dev[i].cond.kbd.key[j] < 0x1e?
			dev[i].cond.kbd.key[j] + 6 :
			dev[i].cond.kbd.key[j] - 0x1d));
		break;
	      case 0x4f:
		ac = -3;
		break;
	      case 0x50:
		ac = -5;
		break;
	      case 0x52:
		ac = -4;
		break;
	      case 0x51:
		ac = -6;
		break;
	      case 0x29:
		ac = -2;
		break;
	      case 0x4c:
		ac = -7;
		break;
	      case 0x28:
		ac = -8;
		break;
	     }
	   break;
	}
    if(ac != last) {
      last = ac;
      acnt = 0;
    } else if(++acnt<8)
      ac = -1;
    else
      acnt = 0;

    if(ac >= 0) {
      fillrect(12*(5+3*(editpos>>1)+(editpos&1)), 7*24,
	       12, 24, 0, 0, 0);
      if(editpos & 1)
	selected_sysid[editpos>>1] =
	  (selected_sysid[editpos>>1] & 0xf0) | ac;
      else
	selected_sysid[editpos>>1] =
	  (selected_sysid[editpos>>1] & 0x0f) | (ac << 4);
      printat(5+3*(editpos>>1), 7, "%x", selected_sysid[editpos>>1]);
      
      fillrect(12*(5+3*(editpos>>1)+(editpos&1)), 8*24,
	       12, 24, 0, 0, 0);
      editpos ++;
      editpos &= 15;
    } else switch(ac) {
    case -2:
      return;
    case -3: case -5:
      fillrect(12*(5+3*(editpos>>1)+(editpos&1)), 8*24,
	       12, 24, 0, 0, 0);
      editpos = (editpos + ac + 4) & 15;
      break;
    case -4: case -6:
      fillrect(12*(5+3*(editpos>>1)+(editpos&1)), 7*24,
	       12, 24, 0, 0, 0);
      if(editpos & 1)
	selected_sysid[editpos>>1] =
	  (selected_sysid[editpos>>1] & 0xf0) |
	  ((selected_sysid[editpos>>1] + ac + 5) & 0xf);
      else
	selected_sysid[editpos>>1] =
	  (selected_sysid[editpos>>1] & 0x0f) |
	  ((selected_sysid[editpos>>1] + (ac<<4) + 80) & 0xf0);
      printat(5+3*(editpos>>1), 7, "%x", selected_sysid[editpos>>1]);
      break;
    case -7:
      fillrect(0, 7*24, 640, 24, 0, 0, 0);
      for(i=0; i<8; i++)
	printat(5+3*i, 7, "%x", selected_sysid[i]=si.id[i]);
      break;
    case -8:
      save_fake_id();
      return;
    }
  }
}

void sysinfo()
{
  unsigned char *ptr;
  struct mapledev *dev;
  int i, j, bk;
  static char *region[] = { "JAPAN", "AMERICA", "EUROPE" };
  static char *lang[] = { "JAPANESE", "ENGLISH", "GERMAN", "FRENCH",
			  "SPANISH", "ITALIAN" };
  static char *bcast[] = { "NTSC", "PAL", "PAL_M", "PAL_N" };
  struct sysinfo si;
  unsigned char sec[60];

 again:
  state = -1;
  clrscr(0);
  get_sysinfo(&si);
  printat(17, 0, "System ID");
  for(i=0; i<8; i++)
    printat(17, i+1, "%x", si.id[i]);
  if(memcmp(si.id, selected_sysid, 8))
    for(i=0; i<8; i++)
      printat(20, i+1, "%C%x", 0xf800, selected_sysid[i]);
  printat(0, 0, "Factory Config");
  for(i=0; i<16; i++)
    printat(0, i+1, "%x", si.settings[i]);
  if(si.settings[2]>='0' && si.settings[2]<='2')
    printat(4, 2, "%s", region[si.settings[2]-'0']);
  if(si.settings[3]>='0' && si.settings[3]<='5')
    printat(4, 3, "%s", lang[si.settings[3]-'0']);
  if(si.settings[4]>='0' && si.settings[4]<='3')
    printat(4, 4, "%s", bcast[si.settings[4]-'0']);
  if(flash_read_sector(2,5,(char*)sec)==1) {
    printat(30, 0, "Settings");
    for(i=0; i<16; i++)
      printat(30, i+1, "%x", sec[i]);    
    if(sec[5]<sizeof(lang)/sizeof(lang[0]))
      printat(34, 6, "%s", lang[sec[5]]);
    if(!(sec[6]&0xfe))
      printat(34, 7, "%s", (sec[6]? "MONO":"STEREO"));
    if(!(sec[7]&0xfe))
      printat(34, 8, "%s", (sec[6]? "AUTOSTART ON":"AUTOSTART OFF"));
  }

  printat(0, 17, "Press %z or F1 to enter a fake System ID", 0xf);
  printat(0, 18, "Press %z or ESC to return to main screen", 0xc);
  /*
  for(i=0; i<128; i++) {
    int j;
    static unsigned char buf[1024];
    syscall_read_flash(i*1024, buf, 1024);
    for(j=0; j<1024; j++)
      serial_send(buf[j]);
  }
  */
  bk=3;
  for(;;) {
    cable_monitor();
    if(dev = check_pads())
      for(i=0; i<4; i++)
	switch(dev[i].func) {
	 case MAPLE_FUNC_CONTROLLER:
	   if(!(dev[i].cond.controller.buttons & 2))
	     bk &= ~1;
	   if(!(dev[i].cond.controller.buttons & 0x400)) {
	     edit_sysid();
	     goto again;
	   }
	   break;
	 case MAPLE_FUNC_KEYBOARD:
	   for(j=0; j<6; j++)
	     switch(dev[i].cond.kbd.key[j]) {
	      case 0x29:
	        bk &= ~1;
		break;
	      case 0x3a:
		edit_sysid();
		goto again;
	     }
	   break;
	}
    if(bk == 2)
      bk = 3;
    else if(!bk)
      break;
    else
      bk = 1;
  }
}

void initialize()
{
  struct sysinfo si;
  extern void uninstall_crack(void);

  uninstall_crack();

  cdfs_init();
  init_tmr0();
  serial_init();
  clrscr(0);
  init_video(check_cable(), 1);
  init_twiddletab();   
  maple_init();

  get_sysinfo(&si);
  memcpy(selected_sysid, si.id, 8);

  restore_fake_id();
}

int main()
{
  unsigned int param[4];
  struct mapledev *dev;
  int i, j, sercnt=0;
  extern void vmoper();

  initialize();

  state = -1;

  for(;;) {
    cable_monitor();
    if(serial_recv()=='\n')
      if(++sercnt>=3) {
	clrscr(0);
	printat(0,0,"Serial slave mode engaged...");
	serial_slave();
	state = -1;
	sercnt = 0;
      }
    gdGdcGetDrvStat(param);
    switch(param[0]) {
     case 0:
       if(state < STATE_DISK) set_state(STATE_DISKTMP, param[1]); break;
       case 1: case 2: case 3: case 4: case 5:
       if(state != STATE_DISK) set_state(STATE_DISK, param[1]); break;
     case 6:
       if(state != STATE_OPEN) set_state(STATE_OPEN, param[1]); break;
     case 7:
       if(state != STATE_NODISK) set_state(STATE_NODISK, param[1]); break;
    }
    if(dev = check_pads())
      for(i=0; i<4; i++)
	switch(dev[i].func) {
	 case MAPLE_FUNC_CONTROLLER:
	   if(!(dev[i].cond.controller.buttons & 8) &&
	      state == STATE_DISK) {
	     try_boot();
	     state = -1;
	   } else if(!(dev[i].cond.controller.buttons & 0x400))
	     vmoper();
	   else if(!(dev[i].cond.controller.buttons & 0x200))
	     sysinfo();
	   break;
	 case MAPLE_FUNC_KEYBOARD:
	   for(j=0; j<6; j++)
	     switch(dev[i].cond.kbd.key[j]) {
	      case 0x3a:
		vmoper();
		break;
	      case 0x3b:
		sysinfo();
		break;
	      case 0x28:
		if(state == STATE_DISK)
		  try_boot();
		break;
	      case 0x46:
		if(dev[i].cond.kbd.shift & 0x11)
		  (*(void(**)())0x8c0000e0)(0);
		break;
	     }
	   break;
	}
  }

  return 0;
}
