; GEOS-W128: WiC64 driver for applications on a native Commodore 128
;
; unsigned char __fastcall__ wic_exec(unsigned char command);
;   r0 = data to send, r1 = its length
;   r2 = buffer for the answer, r3 = its size (the rest is dropped)
;   returns the WiC64 status (0 = ok), WIC_NO_DEVICE ($FF) on a timeout;
;   r4 = number of bytes of the answer stored in the buffer
;
; Protocol: github.com/WiC64-Team/wic64-library, standard protocol "R".
; Every byte is acknowledged through FLAG2 (bit 4 of $DD0D). The transfer
; runs at 1 MHz with interrupts off, like the disk drivers.

	.export _wic_exec

r0	= $02
r1	= $04
r2	= $06
r3	= $08
r4	= $0a

config	= $ff00
CIOIN	= $7e
clkreg	= $d030
PRA	= $dd00         ; bit 2 (PA2): direction of the transfer
PRB	= $dd01
DDRA	= $dd02
DDRB	= $dd03
ICR	= $dd0d

SEND_TIMEOUT = 2        ; about 2 seconds, for each byte sent
ANSWER_TIMEOUT = 40     ; the WiC64 may be busy: connecting, DNS, HTTP

	.bss
command:	.res 1
status:		.res 1
timeout:	.res 1
ticks:		.res 1
remaining:	.res 2

	.code

_wic_exec:
	sta command
	php
	sei
	lda config
	pha
	lda #CIOIN
	sta config
	lda clkreg
	pha
	lda #0
	sta clkreg
	sta r4
	sta r4+1
	lda #$ff
	sta status

	lda ICR                 ; clear FLAG2
	lda DDRA
	ora #%00000100
	sta DDRA
	lda PRA
	ora #%00000100          ; PA2 high: the WiC64 listens
	sta PRA
	lda #$ff
	sta DDRB

	lda #SEND_TIMEOUT
	sta timeout
	lda #'R'
	jsr send
	bcs @bail
	lda command
	jsr send
	bcs @bail
	lda r1
	jsr send
	bcs @bail
	lda r1+1
	jsr send
	bcs @bail

	ldy #0
@data:	lda r1
	ora r1+1
	beq @answer
	lda (r0),y
	jsr send
	bcs @bail
	iny
	bne @1
	inc r0+1
@1:	lda r1
	bne @2
	dec r1+1
@2:	dec r1
	jmp @data

@bail:	jmp done

@answer:
	lda #0
	sta DDRB
	lda PRA
	and #%11111011          ; PA2 low: the WiC64 answers
	sta PRA
	lda #ANSWER_TIMEOUT
	sta timeout
	jsr wait                ; it confirms the change of direction
	bcs done
	lda PRB                 ; our handshake: ready to receive
	jsr get
	bcs done
	pha                     ; the status, stored when it all worked
	jsr get
	bcs fail
	sta remaining
	jsr get
	bcs fail
	sta remaining+1

	ldy #0
@recv:	lda remaining
	ora remaining+1
	beq @ok
	jsr get
	bcs fail
	ldx r3+1                ; room left in the buffer?
	bne @store
	ldx r3
	beq @next
@store:	sta (r2),y
	iny
	bne @3
	inc r2+1
@3:	lda r3
	bne @4
	dec r3+1
@4:	dec r3
	inc r4
	bne @next
	inc r4+1
@next:	lda remaining
	bne @5
	dec remaining+1
@5:	dec remaining
	jmp @recv

@ok:	pla
	sta status
	jmp done
fail:	pla
done:	lda #0
	sta DDRB
	lda ICR
	pla
	sta clkreg
	pla
	sta config
	plp
	lda status
	ldx #0
	rts

send:	sta PRB
wait:	txa
	pha
	tya
	pha
	lda timeout
	sta ticks
	ldx #0
	ldy #0
@1:	lda ICR
	and #%00010000
	bne @ok
	dex
	bne @1
	dey
	bne @1
	dec ticks
	bne @1
	pla
	tay
	pla
	tax
	sec
	rts
@ok:	pla
	tay
	pla
	tax
	clc
	rts

get:	jsr wait
	bcs @rts
	lda PRB
	clc
@rts:	rts
