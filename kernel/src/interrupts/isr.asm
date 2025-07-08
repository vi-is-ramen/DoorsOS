%macro pushad 0
    push rax
    push rcx
    push rdx
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
%endmacro

%macro popad 0
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rax
%endmacro

%macro isr_err_stub 1
isr_stub_%+%1:
    push %1
    jmp isr_common_stub
%endmacro

%macro isr_no_err_stub 1
isr_stub_%+%1:
    push 0
    push %1
    jmp isr_common_stub
%endmacro

section .text
global isr_stub_table
global irq_stub_table
extern exception_handler
extern irq_handler

; Common ISR handler stub
isr_common_stub:
    pushad
    cld
    lea rdi, [rsp]
    call exception_handler
    popad
    add rsp, 8          ; remove pushed interrupt number
    iretq

; Common IRQ handler stub with PIC EOI
irq_common_stub:
    pushad
    cld
    lea rdi, [rsp]
    call irq_handler
    popad

    ; Send EOI to master PIC
    mov al, 0x20
    out 0x20, al

    ; Check if interrupt vector >= 40 (IRQ8+), send EOI to slave PIC
    mov rax, [rsp + 8]  ; Interrupt vector number pushed earlier
    cmp rax, 40
    jb .skip_slave_eoi
    out 0xA0, al
.skip_slave_eoi:

    add rsp, 8          ; remove pushed interrupt number
    iretq

; ISRs for exceptions 0-31
isr_no_err_stub 0
isr_no_err_stub 1
isr_no_err_stub 2
isr_no_err_stub 3
isr_no_err_stub 4
isr_no_err_stub 5
isr_no_err_stub 6
isr_no_err_stub 7
isr_err_stub    8
isr_no_err_stub 9
isr_err_stub    10
isr_err_stub    11
isr_err_stub    12
isr_err_stub    13
isr_err_stub    14
isr_no_err_stub 15
isr_no_err_stub 16
isr_err_stub    17
isr_no_err_stub 18
isr_no_err_stub 19
isr_no_err_stub 20
isr_no_err_stub 21
isr_no_err_stub 22
isr_no_err_stub 23
isr_no_err_stub 24
isr_no_err_stub 25
isr_no_err_stub 26
isr_no_err_stub 27
isr_no_err_stub 28
isr_no_err_stub 29
isr_err_stub    30
isr_no_err_stub 31

; IRQ stubs for IRQ0-15 (interrupt vectors 32-47)
%assign i 32
%rep 16
irq_stub_%+i:
    cli
    push i
    jmp irq_common_stub
    %assign i i+1
%endrep

; ISR stub table: pointers to ISRs 0-31
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_%+i
    %assign i i+1
%endrep

; IRQ stub table: pointers to IRQs 32-47
irq_stub_table:
%assign i 32
%rep 16
    dq irq_stub_%+i
    %assign i i+1
%endrep
