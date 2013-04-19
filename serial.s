	.text
	
	.globl	_serial_slave, _serial_init, _serial_recv, _serial_send


addr0:	
	bra start2
	nop
	
	bra reactivate
	nop

	bra ccache2
	nop

	bra disable_cache
	nop

	bra start0
	nop

_serial_slave:
	mov.l	caddr0,r1
	mov.l	caddr1,r2
	mov.l	caddr2,r3
	sub	r1,r2
	mov	r3,r4
	shlr2	r2
	add	#16,r4
.copyloop:
	mov.l	@r1+,r0
	dt	r2
	mov.l	r0,@r3
	bf/s	.copyloop	
	add	#4,r3
	jmp	@r4
	nop

	.align	4
caddr0:	
	.long	addr0
caddr1:	
	.long	addrn
caddr2:	
	.long	0xac00e000

start0:
	mov.l	r14,@-r15
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	sts.l	pr,@-r15
	mov.l	r11,@-r15
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov.l	r8,@-r15
	
	mov	#-24,r14
	shll16	r14
	add	#12,r14

	mov.l	dcptr,r2
	jsr	@r2
	nop

	bra	prompt
	nop
	
start2:	
	mov.l	wtcsr,r1
	mov.w	wtdata,r0
	mov.w	r0,@r1
	nop
	bsr	send
	mov	#0,r4
.emptyrcv:
	bsr	recv
	nop
	cmp/pz	r0
	bt	.emptyrcv
	bra	prompt
	nop

	
lprompt:
	bsr	recvw
	nop
	cmp/eq	#'\n',r0
	bf	lprompt
prompt:
	mov.l	blue,r4
	bsr	set_border
	nop
	bsr	send
	mov	#'>',r4
waitinp:
	bsr	recvw
	nop
	cmp/eq	#'\r',r0
	bt	waitinp
	cmp/eq	#'\n',r0
	bt	prompt
	cmp/eq	#'?',r0
	bf	.noeq
	bsr	send
	mov	#'V',r4
	mov.l	version,r4
	bsr	senddec
	mov	#0,r5
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	bra	lprompt
	nop

set_border:
	mov.l	border_addr,r0
	rts
	mov.l	r4,@r0

	.align	4

version:	
	.long	104
blue:
	.long	0x000000ff
border_addr:
	.long	0xa05f8040
wtcsr:
	.long	0xffc0000c
wtdata:
	.word	0xa500


.noeq:
	cmp/eq	#'S',r0
	bt	srec
badcmd:
	bsr	send
	mov	#'?',r4
	bsr	send
	mov	#'C',r4
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	bra	lprompt
	nop
	
syntax:
	mov	r0,r5
	bsr	send
	mov	#'?',r4
	bsr	send
	mov	#'S',r4
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	mov	r5,r0
	cmp/eq	#'\n',r0
	bt	prompt
	bra	lprompt
	nop

srec:
	bsr	recvw
	nop
	mov	#'0',r1
	cmp/ge	r1,r0
	bf	syntax
	mov	#'9',r1
	cmp/gt	r1,r0
	bt	syntax
	add	#-'0',r0
	shll	r0
	mov	r0,r1
	mova	.srtab,r0
	add	r0,r1
	mov.w	@r1,r1
	extu.w	r1,r1
	add	r0,r1
	jmp	@r1
	.align	4
.srtab:
	.word	srec0-.srtab
	.word	srec1-.srtab
	.word	srec2-.srtab
	.word	srec3-.srtab
	.word	srec4-.srtab
	.word	srec5-.srtab
	.word	srec6-.srtab
	.word	srec7-.srtab
	.word	srec8-.srtab
	.word	srec9-.srtab

srec0:
to_lprompt:	
	bra	lprompt
	nop

srec3:
	mov	#0,r10
	bsr	getbyt
	mov	#0,r11
	bf	syntax
	bsr	getbyt
	mov	r10,r9
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	mov	r10,r12
	add	#-5,r9
	cmp/pl	r9
	bf	.chkcsum
	shll2	r12
	shll	r12
	mov	#5,r0
	or	r0,r12
	rotr	r12
	rotr	r12
	rotr	r12
.s3loop:
	bsr	getbyt
	add	#-1,r9
	bf	syntax
	mov.b	r10,@r12
	add	#1,r12
	cmp/pl	r9
	bt	.s3loop
.chkcsum:
	not	r11,r11
	extu.b	r11,r9
	bsr	getbyt
	nop
	bf	syntax
	extu.b	r10,r0
	cmp/eq	r0,r9
	bt	to_lprompt
badcsum:	
	bsr	send
	mov	#'?',r4
	bsr	send
	mov	#'X',r4
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	bra	lprompt
	nop


srec7:
	mov	#0,r10
	bsr	getbyt
	mov	#0,r11
	bf	syntax
	mov	#5,r1
	cmp/eq	r1,r10
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	bsr	getbyt
	nop
	bf	syntax
	bsr	getbyt
	nop
	bf	to_syntax
	mov	r10,r12
	not	r11,r11
	extu.b	r11,r9
	bsr	getbyt
	nop
	bf	to_syntax
	extu.b	r10,r0
	cmp/eq	r0,r9
	bf	badcsum
.wnls7:
	bsr	recvw
	nop
	cmp/eq	#'\n',r0
	bf	.wnls7
	bsr	send
	mov	#'O',r4
	bsr	send
	mov	#'K',r4
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	bsr	flush
	nop

	mov.l	red,r4
	bsr	set_border
	nop
	bra	launch
	nop
reactivate:
	mov.l	ocav,r0
	mov	#2,r1
	shll8	r1
	mov	#0,r2
.clroca:
	dt	r1
	mov.l	r2,@r0
	bf/s	.clroca
	add	#32,r0
	mov.w	cacheinh,r1
	mov.l	dcptr2,r0
	mov.w	r1,@r0

	bsr	_serial_init
	nop

	mov	#-24,r14
	shll16	r14
	add	#12,r14

	bsr	send
	mov	#0,r4
	bsr	send
	mov	#'+',r4
	bsr	send
	nop
	bsr	send
	nop
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	mov.l	start2addr,r0
	jmp	@r0
	nop

to_syntax:
	bra	syntax
	nop

	.align	4

start2addr:
	.long	0x8c00e000 /*START2ADDR*/
ocav:
	.long	0xf4000000
red:
	.long	0x00ff0000
cacheinh:
	.word	0x0808
		
srec1:
srec2:
srec4:
srec5:
srec6:
srec8:
srec9:
	bsr	send
	mov	#'?',r4
	bsr	send
	mov	#'N',r4
	bsr	send
	mov	#'\r',r4
	bsr	send
	mov	#'\n',r4
	bra	lprompt
	nop

getbyt:
	sts	pr,r2
	bsr	recvw
	nop
	bsr	eatnyb
	nop
	bf	.nogetbyt
	bsr	recvw
	nop
	bsr	eatnyb
	nop
.nogetbyt:	
	lds	r2,pr
	rts
	add	r10,r11

eatnyb:
	shll2	r10
	shll2	r10
	mov	#'0',r1
	cmp/ge	r1,r0
	bf	.badnyb
	mov	#'9',r1
	cmp/gt	r1,r0
	bt	.letter
	add	r0,r10
	add	#-'0',r10
	rts
	sett
.letter:
	mov	#'A',r1
	cmp/ge	r1,r0
	bf	.badnyb
	mov	#'F',r1
	cmp/gt	r1,r0
	bt	.badnyb2
	add	r0,r10
	add	#10-'A',r10
	sett
.badnyb:
	rts
	nop
.badnyb2:
	bra	.badnyb
	clrt

senddec:
	mova	buffer,r0
	add	#15,r0
	mov	r0,r3
	mov	#0,r0
	mov.b	r0,@r3
.nextd:	
	mov	r4,r0

	mov	#10,r1
	shll16	r1
	div0u
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	div1	r1,r0
	rotcl	r0
	extu.w	r0,r0
	mov	#10,r1
	mulu	r1,r0
	add	#-1,r3
	add	#'0',r4
	sts	macl,r1
	sub	r1,r4
	mov.b	r4,@r3
	mov	r0,r4
	add	#-1,r5
	tst	r4,r4
	bf	.nextd
	cmp/pl	r5
	bt	.nextd
	bra	sendstr
	mov	r3,r4

sendhex:
	mova	buffer,r0
	add	#15,r0
	mov	r0,r3
	mov	#0,r0
	mov.b	r0,@r3
	mov	#10,r1
.nexth:	
	mov	r4,r0
	and	#15,r0
	add	#-1,r3
	cmp/ge	r1,r0
	add	#'0',r0
	bf	.noalph
	add	#'A'-'0'-10,r0
.noalph:
	mov.b	r0,@r3
	shlr2	r4
	shlr2	r4
	add	#-1,r5
	tst	r4,r4
	bf	.nexth
	cmp/pl	r5
	bt	.nexth
	mov	r3,r4
		
sendstr:
	mov	r4,r3
	sts	pr,r2
.ssloop:	
	mov.b	@r3+,r4
	tst	r4,r4
	bt	.ssdone
	bsr	send
	nop
	bra	.ssloop
	nop
.ssdone:
	lds	r2,pr
	rts
	nop



launch:
	mov.l	cc2ptr,r2
	jsr	@r2
	nop

	stc	sr,r0
	mov.w	imask1,r1
	and	r1,r0
	or	#-16,r0
	ldc	r0,sr

	mov.l	newsp,r0
	mov	r0,r15

	mov.l	dcptr,r2
	jsr	@r2
	nop

	mov.l	ms_strt,r4
	mov.w	ms_len,r6
	bsr	memset
	mov	#0,r5

	mov.l	newsr,r0
	ldc	r0,sr

	sub	r0,r0
	mov	r0,r1
	mov	r0,r2
	mov	r0,r3
	mov	r0,r4
	mov	r0,r5
	mov	r0,r6
	mov	r0,r7
	mov	r0,r8
	mov	r0,r9
	mov	r0,r10
	mov	r0,r11
	mov	r0,r13
	mov	r0,r14

	mov.l	newsp2,r0
	mov	r0,r15
	mov.l	newvbr,r0
	ldc	r0,vbr
	mov.l	newfpscr,r0
	lds	r0,fpscr

	mov	r12,r0
	mov	r1,r12
	jsr	@r0
	mov	r1,r0

	stc	sr,r0
	mov.w	imask1,r1
	and	r1,r0
	or	#-16,r0
	ldc	r0,sr

	mov.l	rebootaddr,r0
	jmp	@r0
	nop


	.align	4

disable_cache:	
	mov.l	dcptr2,r2
	mov	#0,r3
	rts
	mov.l	r3,@r2

ccache2:
	mov.l	dcptr2,r0
	mov.l	@r0,r1
	mov.l	dcmask,r2
	and	r2,r1
	mov.w	dcval,r2
	or	r2,r1
	mov.l	r1,@r0
	rts
	nop

memset:
	mov	#0,r7
	mov	r7,r3
	cmp/hs	r6,r3
	bt/s	.mmbye
	mov	r4,r0
.mmlp:	add	#1,r7
	mov.b	r5,@r0
	cmp/hs	r6,r7
	bf/s	.mmlp
	add	#1,r0
.mmbye:	rts
	mov	r4,r0
	
	.align	4

rebootaddr:	
	.long	0xac00e004 /*REACTIVATEADDR*/
cc2ptr:
	.long	0xac00e008 /*CCACHE2ADDR*/
dcptr:
	.long	0xac00e00c /*DCACHEADDR*/
ncmask:
	.long	0xa0000000
newsp:
	.long	0xac00f400
ms_strt:
	.long	0xac00fc00
newsr:
	.long	0x700000f0
newsp2:
	.long	0x8c00f400
newvbr:
	.long	0x8c00f400
newfpscr:
	.long	0x40001
launchaddr:
	.long	0xac010000
dcptr2:
	.long	0xff00001c
dcmask:
	.long	0x89af
dcval:
	.word	0x800
ms_len:
	.word	0x400
imask1:
	.word	0xff0f
			

	.align	4

	
	
_serial_init:
	mov.l	r14,@-r15
	mov	#-24,r14
	shll16	r14

	! SCSMR2 @r14 16
	! SCBRR2 @(4,r14) 8
	! SCSCR2 @(8,r14) 16
	! SCFTDR2 @(12,r14) 8
	! SCFSR2 @(16,r14) 16
	! SCFRDR2 @(20,r14) 8
	! SCFCR2 @(24,r14) 16
	! SCFDR2 @(28,r14) 16
	! SCSPTR2 @(32,r14) 16
	! SCLSR2 @(36,r14) 16

	mov	#0,r0
	! disable interrupts, disable transmit/receive, use internal clock
	mov.w	r0,@(8,r14)
	! 8N1, use PØ clock
	mov.w	r0,@r14
	! select 57600 bps
	mov	#26,r0
	mov.b	r0,@(4,r14)
	! need to keep in range 4bit...
	! SCSCR2 @r14 16
	! SCFTDR2 @(4,r14) 8
	! SCFSR2 @(8,r14) 16
	! SCFRDR2 @(12,r14) 8
	! SCFCR2 @(16,r14) 16
	! SCFDR2 @(20,r14) 16
	! SCSPTR2 @(24,r14) 16
	! SCLSR2 @(28,r14) 16
	add	#8,r14
	! reset FIFOs, enable hardware flow control
	mov	#12,r0
	mov.w	r0,@(16,r14)
	mov	#8,r0
	mov.w	r0,@(16,r14)
	! disable manual pin control
	mov	#0,r0
	mov.w	r0,@(24,r14)
	! clear status
	mov.w	@(8,r14),r0
	mov	#0x60,r0
	mov.w	r0,@(8,r14)
	mov.w	@(28,r14),r0
	mov	#0,r0
	mov.w	r0,@(28,r14)
	! enable transmit/receive
	mov	#0x30,r0
	mov.w	r0,@r14

	! send data
	! SCFTDR2 @r14 8
	! SCFSR2 @(4,r14) 16
	! SCFRDR2 @(8,r14) 8
	! SCFCR2 @(12,r14) 16
	! SCFDR2 @(16,r14) 16
	! SCSPTR2 @(20,r14) 16
	! SCLSR2 @(24,r14) 16

	add	#4,r14
	mov.w	@(4,r14),r0
	and	#0x6d,r0
	mov.w	r0,@(4,r14)
	mov.w	@(24,r14),r0
	and	#0xfe,r0
	mov.w	r0,@(24,r14)
	rts
	mov.l	@r15+,r14

send:
.sloop:	
	mov.w	@(4,r14),r0
	tst	#0x20,r0
	bt	.sloop
	mov.b	r4,@r14
	nop
	and	#0x9f,r0
	rts
	mov.w	r0,@(4,r14)

recv:
	mov.w	@(16,r14),r0
	tst	#0x1f,r0
	bt	.norecv
	mov.b	@(8,r14),r0
	extu.b	r0,r1
	mov.w	@(4,r14),r0
	and	#0x6d,r0
	mov.w	r0,@(4,r14)
	rts
	mov	r1,r0
.norecv:
	rts
	mov	#-1,r0

recvw:	
	sts	pr,r3
.rcvlp:	
	bsr	recv
	nop
	cmp/pz	r0
	bf	.rcvlp
	lds	r3,pr
	rts
	nop

flush:
	mov.w	@(4,r14),r0
	and	#0xbf,r0
	mov.w	r0,@(4,r14)
.wflush:	
	mov.w	@(4,r14),r0
	tst	#64,r0
	bt	.wflush	
	and	#0xbf,r0
	rts
	mov.w	r0,@(4,r14)
		
_serial_recv:
	mov	#-24,r3
	shll16	r3
	add	#12,r3
	mov.w	@(16,r3),r0
	tst	#0x1f,r0
	bt	.srnorecv
	mov.b	@(8,r3),r0
	extu.b	r0,r1
	mov.w	@(4,r3),r0
	and	#0x6d,r0
	mov.w	r0,@(4,r3)
	rts
	mov	r1,r0
.srnorecv:
	rts
	mov	#-1,r0

_serial_send:
.sssloop:	
	mov	#-24,r3
	shll16	r3
	add	#12,r3
	mov.w	@(4,r3),r0
	tst	#0x20,r0
	bt	.sssloop
	mov.b	r4,@r3
	nop
	and	#0x9f,r0
	rts
	mov.w	r0,@(4,r3)
		

	.align	4
buffer:
	.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

addrn:

			
	.end
	

