
	.globl	_launch, _set_fake_sysid, _install_crack, _uninstall_crack

	.text
	
_launch:
	mov	r4,r14

	mov.l	newsr,r0
	ldc	r0,sr
	mov.l	newsp,r0
	mov	r0,r15

	mov.l	cc2ptr,r2
	mov.l	ncmask,r3
	or	r3,r2
	jsr	@r2
	nop

	mov.l	newgbr,r0
	ldc	r0,gbr

	mov.l	ms_strt,r4
	mov.w	ms_len,r6
	bsr	memset
	mov	#0,r5

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
	mov	r0,r12
	mov	r0,r13

	lds	r0,macl
	lds	r0,mach
	clrt
	
	mov.l	newsp2,r0
	mov	r0,r15
	mov.l	newvbr,r0
	ldc	r0,vbr
	mov.l	newfpscr,r0
	lds	r0,fpscr

	mov.l	startaddr,r0
	mov.l	ncmask,r1
	or	r1,r0
	lds	r0,pr

	mov	r2,r0
	mov	r0,r1
	jmp	@r14
	mov	r0,r14


	.align	4

ccache2:
	sts	pr,r1
	mov.l	ncmask,r0
	or	r0,r1
	lds	r1,pr
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

cc2ptr:
	.long	ccache2
ncmask:
	.long	0xa0000000
newgbr:
	.long	0x8c000000
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
startaddr:
	.long	start
newfpscr:
	.long	0x40001
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

	.align  4

_uninstall_crack:
	mov.l	vector_bc,r0
	mov.l	@r0,r0
	mov.l	vbc_mask,r1
	and	r0,r1
	mov.l	vbc_compare,r2
	cmp/ge	r2,r1
	bf	do_vector_init
	mov.l	vbc_default,r0
do_vector_init:
	mov	#-1,r6
	jmp	@r0
	mov	#0,r7

	.align	2

vector_bc:
	.long	0x8c0000bc
vbc_mask:
	.long	0x1fffffff
vbc_compare:
	.long	0x0c008000
vbc_default:
	.long	0x8c001000

	.align  4

_set_fake_sysid:
	mova	fake_id+8,r0
	mov	#8,r2
.copy_fake_sysid:
	dt	r2
	mov.b	@r4+,r1
	bf/s	.copy_fake_sysid
	mov.b	r1,@-r0
	rts
	nop	
	
	.align	4

_install_crack:
	mova	crack_start,r0
	mov.l	install_addr,r3
	mov	#(crack_end-crack_start)/4,r2
.cploop:
	mov.l	@r0+,r1
	dt	r2
	mov.l	r1,@r3
	bf/s	.cploop
	add	#4,r3
	add	#-(crack_end-the_real_one),r3
	mov.l	sysc_vector,r1
	mov.l	@r1,r0
	mov.l	r0,@r3
	mov.l	@(4,r1),r0
	mov.l	r0,@(4,r3)
	add	#-(the_real_one-set_sysvec_a),r3
	mov	r4,r0
	tst	#2,r0
	bf	.keep_a
	mov	#9,r1
	mov.w	r1,@r3
.keep_a:
	tst	#1,r0
	add	#-(set_sysvec_a-part2),r3
	bf	.keep_b
	mov	#8,r1
	mov.w	r1,@r3
.keep_b:	
	bra	low_patch_sysvec
	nop

	.align	4

crack_start:
	mov #1,r1
	cmp/eq r1,r7
	mov.l the_real_one,r0
	bf .old_syscall0
	mov.l sysid_offs,r1
	sub r4,r1
	cmp/pz r1
	bf .old_syscall0
	sub r6,r1
	add #8,r1
	cmp/pl r1
	bf .fake_sysid
.old_syscall0:	
	jmp @r0
	nop

	.word 0xa00e, 0x0009	
part2:
	tst r6,r6
	mov.l the_real_one+4,r0
	bf .old_syscall
	mov #4,r1
	cmp/eq r1,r7
	bf .old_syscall
	sts.l pr,@-r15
	jsr @r0
	mov.l r4,@-r15
	mov.w gdrom,r1
	mov.l @r15+,r4
	bra leave_sc
	lds.l @r15+,pr
gdrom:
	.word 0x80

	.word 0xa00e, 0x0009
.old_syscall:
	sts.l pr,@-r15
	jsr @r0
	nop
	lds.l @r15+,pr
low_patch_sysvec:
	mov.l sysc_vector,r4
	mov.l install_addr,r1
set_sysvec_a:
	mov.l r1,@r4
	add   #part2-crack_start,r1
leave_sc:
	rts
set_sysvec_b:
	mov.l r1,@(4,r4)

	.align  2
sysc_vector:
	.long 0x8c0000b8
install_addr:
	.long 0x8c00b764

	.word 0xa00e, 0x0009

.fake_sysid:
	add r6,r1
	add r5,r1
	sts.l pr,@-r15
	jsr @r0
	mov.l r1,@-r15
	mov.l @r15+,r1
	lds.l @r15+,pr
	mov.l r0,@-r15
	bra .fakeid2
	mov #8,r2

	.align 2

the_real_one:
	.long 0, 0

	.word 0xa00e, 0x0009

.fakeid2:
	mova fake_id,r0
.loop:	dt r2
	mov.b @r0+,r3
	bf/s .loop
	mov.b r3,@-r1
	rts
	mov.l @r15+,r0

	.align 2

sysid_offs:
	.long 0x1a056
fake_id:
	.byte 0,0,0,0,0,0,0,0
	
crack_end:
	
	.end


