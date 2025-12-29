ORG 0x100

; Setup parameters for printcallback
MOV AL, 'A'

; Setup callback, call printdirect 10 times
MOV CX, 10
MOV SI, printdirect
CALL repeatcallback

; Exit
MOV AH, 0x4C
INT 21h

repeatcallback:
PUSH CX

TEST CX, CX

repeatcallbackloop:
JZ repeatcallbackend

; If SI / CX are not preserved, push and pop
; around the call (not done here)
CALL SI

DEC CX
JMP repeatcallbackloop

repeatcallbackend:
POP CX
RET

;;;;;;;;;;;;;;;; printdirect: print byte in AL
;;;;;;;;;;;;;;;; IN: AL byte to be printed
printdirect:
PUSH AX
PUSH DX

MOV DL, AL
MOV AH, 0x02
INT 0x21

POP DX
POP AX

RET
