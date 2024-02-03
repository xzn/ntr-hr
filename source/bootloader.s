.arm
.align(4);
.section .text
.global _Reset
_Reset:
b _Start
.global _BootArgs
_BootArgs:
nop
nop
nop
nop
nop
nop
nop
nop
_Start:
STMFD   SP!, {R0-R12, LR};
MRS     R0, CPSR
STMFD   SP!, {R0}

LDR     R6, =_Reset
ADR     R5, _Reset
sub     r5, r5, r6 /* r5 = realAddress - baseAddress */
ldr     r6, =__rel_dyn_start
ldr     r7, =__rel_dyn_end
add     r6, r6, r5
add     r7, r7, r5
relocNotFinished:
ldmia   r6!, {r3, r4}
cmp     r4, #0x17
bne     notRelativeEntry
add     r3, r3, r5
ldr     r4, [r3]
add     r4, r4, r5
str     r4, [r3]
notRelativeEntry:
cmp     r6, r7
bcc     relocNotFinished
ldr     r0, =0xffff8001
adr     r1, _Reset
ldr     r2, =__rel_dyn_end
sub     r2, r2, r1 /* r2 = codesize */
svc     0x54       /* flush instruction cache */
nop
nop

mov     r0, sp
bl      c_entry

ldmfd   sp!, {r0}
msr     cpsr, r0
ldmfd   SP!, {R0-R12, LR};

.global _ReturnToUser
_ReturnToUser:
bx lr
nop
nop
nop
msr cpsr, r0
/* unused
ldr     PC, =c_entry
*/

.section .__rel_dyn_start
__rel_dyn_start:

.section .__rel_dyn_end
__rel_dyn_end:

.section .__bss_start

.section .__bss_end
