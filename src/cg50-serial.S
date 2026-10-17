/* fx-CG50 / Graph 90+E OS serial port through the syscall table
   (prizm.cemetech.net, Syscalls/Serial): Serial_Open 0x1BB7,
   Serial_Close 0x1BB8, Serial_IsOpen 0x1BC6. Arguments stay in r4. */
.text
.balign 2
.global _cg50_serial_open
.global _cg50_serial_close
.global _cg50_serial_state

#define syscall(id)		\
	mov.l	1f, r2		;\
	mov.l	2f, r0		;\
	jmp	@r2		;\
	nop			;\
.balign 4			;\
1:	.long	0x80020070	;\
2:	.long	id

_cg50_serial_open:
	syscall(0x1bb7)
_cg50_serial_close:
	syscall(0x1bb8)
_cg50_serial_state:
	syscall(0x1bc6)
