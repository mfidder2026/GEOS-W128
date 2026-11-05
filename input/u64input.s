; GEOS-U64 input driver: 1351 mouse in port 1 and joysticks in port 1
; and port 2, all at the same time; use whatever is plugged in.
;
; - mouse: SID pots of port 1, linear (the Ultimate has its own mouse
;   sensitivity and acceleration settings)
; - joystick: directions of both ports, speeding up from 1 to 8 pixels
;   per frame while held
; - fire button of either port (= left mouse button) clicks
;
; Clamping to the screen is left to the kernal (CheckMsePos).

.include "const.inc"
.include "geossym.inc"
.include "geosmac.inc"
.include "c64.inc"
.include "config.inc"

.segment "inputdrv"

MouseInit:
	jmp _MouseInit
SlowMouse:
	jmp _SlowMouse
UpdateMouse:
	jmp _UpdateMouse
.ifdef bsw128
SetMouse:
	rts
.endif

tmpFire:
	.byte 0
mseOld:
	.byte 0, 0              ; last pot values X, Y
joySpeed:
	.byte 0

_MouseInit:
	LoadW mouseXPos, 8
	LoadB mouseYPos, 8
_SlowMouse:
	rts

_UpdateMouse:
	bbsf MOUSEON_BIT, mouseOn, @on
	rts
@on:	PushB CPU_DATA
	LoadB CPU_DATA, IO_IN
	PushW cia1base+2
	PushB cia1base+0
	LoadW cia1base+2, 0     ; both ports input: joysticks readable
	LoadB r1L, 0            ; direction bits for inputData

	; fire of port 1 and 2 (bit 4 low = pressed)
	lda cia1base+0
	and cia1base+1
	pha
	and #$10
	cmp tmpFire
	beq @dirs
	sta tmpFire
	asl
	asl
	asl
	sta mouseData           ; $80 = released, 0 = pressed
	smbf MOUSE_BIT, pressFlag

	; joystick directions: bit 0 up, 1 down, 2 left, 3 right (low active)
@dirs:	pla
	eor #$0f
	and #$0f
	bne @joy
	sta joySpeed            ; released: start slow again
	beq @mouse              ; always
@joy:	sta r0L
	lda joySpeed
	cmp #$1c
	bcs @fast
	inc joySpeed
@fast:	lsr
	lsr
	sta r0H
	inc r0H                 ; step 1..8
	lsr r0L                 ; up
	bcc @1
	jsr @negStep
	jsr addY
@1:	lsr r0L                 ; down
	bcc @2
	lda r0H
	jsr addY
@2:	lsr r0L                 ; left
	bcc @3
	jsr @negStep
	jsr addX
@3:	lsr r0L                 ; right
	bcc @mouse
	lda r0H
	jsr addX

	; 1351: select the pots of port 1 and let the SID sample them
@mouse:	LoadB cia1base+2, $ff
	LoadB cia1base+0, $40
.if .defined(ultimateTurbo) || .defined(w128)
	lda clkreg              ; ~1 ms of real time, also in turbo mode
	pha
	LoadB clkreg, 0
.endif
	ldx #$66
@wait:	nop
	nop
	nop
	dex
	bne @wait
.if .defined(ultimateTurbo) || .defined(w128)
	pla
	sta clkreg
.endif
	lda sidbase+$19
	ldy #0
	jsr CountDelta
	jsr addX
	lda sidbase+$1a
	ldy #1
	jsr CountDelta
	eor #$ff                ; pot Y counts up when moving up
	clc
	adc #1
	jsr addY

	lda r1L
	lsr
	lsr
	lsr
	lsr
	tax
	lda mseDirTab,x
	sta inputData
	PopB cia1base
	PopW cia1base+2
	PopB CPU_DATA
	rts

@negStep:
	lda r0H
	eor #$ff
	clc
	adc #1
	rts

; A = pot value, Y = 0 (X) or 1 (Y). Returns the signed movement in A;
; the old value is only replaced when the mouse moved (noise stays out).
CountDelta:
	tax
	sec
	sbc mseOld,y
	and #$7f
	cmp #$40
	bcs @neg
	lsr
	beq @zero
	bne @moved              ; always
@neg:	ora #$c0
	cmp #$ff
	beq @zero
	sec
	ror
@moved:	pha
	txa
	sta mseOld,y
	pla
	rts
@zero:	lda #0
	rts

; mouseXPos += A (signed); sets $80 (left) or $40 (right) in r1L
addX:	ldx #0
	ora #0
	beq @r
	bpl @p
	dex
	pha
	lda #$80
	bne @set                ; always
@p:	pha
	lda #$40
@set:	ora r1L
	sta r1L
	pla
	clc
	adc mouseXPos
	sta mouseXPos
	txa
	adc mouseXPos+1
	sta mouseXPos+1
@r:	rts

; mouseYPos += A (signed); sets $20 (up) or $10 (down) in r1L
addY:	ora #0
	beq @r
	pha
	bmi @up
	lda #$10
	bne @set                ; always
@up:	lda #$20
@set:	ora r1L
	sta r1L
	pla
	clc
	adc mouseYPos
	sta mouseYPos
@r:	rts

; GEOS direction for inputData, index = left, right, up, down bits
mseDirTab:
	.byte $ff, $06, $02, $ff, $00, $07, $01, $ff
	.byte $04, $05, $03, $ff, $ff, $ff, $ff, $ff
