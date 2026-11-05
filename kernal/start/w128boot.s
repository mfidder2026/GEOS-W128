; GEOS-W128: purgeable boot code for a native Commodore 128
;
; - REU detection: fills in ramExpSize for Configure
; - clock: date and time from a WiC64 on the userport, if there is one
;
; WiC64 protocol: github.com/WiC64-Team/wic64-library. Every byte is
; acknowledged through the FLAG2 line (bit 4 of $DD0D).

.include "const.inc"
.include "geossym.inc"
.include "geosmac.inc"
.include "config.inc"
.include "kernal.inc"
.include "c64.inc"

.ifdef w128

.global W128Boot
.import dateCopy

WIC64_GET_LOCAL_TIME = $15
TIME_LENGTH = 20                ; "15:23:03 31-12-2023",0
REU_MAX_BANKS = 8               ; 512 KB, what Configure 2.0 handles

.segment "start"

; called with I/O banked in and the VIC bank (= REU DMA bank) set to 1
W128Boot:
	jsr DetectREU
	ldy #2                  ; a date, should there be no WiC64
@1:	lda dateCopy,y
	sta year,y
	dey
	bpl @1
	; fall through

SetClock:
	lda cia2base+13         ; clear FLAG2
	lda cia2base+2
	ora #%00000100          ; PA2 is an output
	sta cia2base+2
	lda cia2base
	ora #%00000100          ; PA2 high: the WiC64 listens
	sta cia2base
	LoadB cia2base+3, $ff
	ldy #0
@send:	lda request,y
	sta cia2base+1
	jsr WaitFlag
	bcs @end
	iny
	cpy #4
	bne @send

	LoadB cia2base+3, 0
	lda cia2base
	and #%11111011          ; PA2 low: the WiC64 answers
	sta cia2base
	jsr WaitFlag            ; it confirms the change of direction
	bcs @end
	lda cia2base+1          ; our handshake: ready to receive
	jsr GetByte             ; status
	bcs @end
	pha
	jsr GetByte             ; size low
	sta r0L
	jsr GetByte             ; size high
	pla
	bne @end                ; status: the WiC64 has no time
	ldy #0
@recv:	cpy r0L
	beq @done
	jsr GetByte
	bcs @end
	cpy #TIME_LENGTH
	bcs @skip
	sta timeText,y
@skip:	iny
	bne @recv
@done:	cpy #TIME_LENGTH
	bne @end
	jsr SetTime
@end:	LoadB cia2base+3, 0
	lda cia2base+13
	rts

GetByte:
	jsr WaitFlag
	bcs @rts
	lda cia2base+1
@rts:	rts

; C=1 if FLAG2 does not come within about a second
WaitFlag:
	txa
	pha
	tya
	pha
	ldy #0
	ldx #0
@1:	lda cia2base+13
	and #%00010000
	bne @ok
	dex
	bne @1
	dey
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

; "HH:MM:SS DD-MM-YYYY" into the GEOS variables and the CIA clock
SetTime:
	ldx #17                 ; YY
	jsr Number
	sta year
	ldx #12
	jsr Number
	sta month
	ldx #9
	jsr Number
	sta day
	ldx #0
	jsr Number
	sta hour
	ldx #3
	jsr Number
	sta minutes
	ldx #6
	jsr Number
	sta seconds

	ldx #0
	jsr BCD
	cmp #$13                ; CIA hour register: 12 hours + PM flag
	bcc @am
	sed
	sec
	sbc #$12
	cld
	ora #$80
@am:	sta cia1base+11
	ldx #3
	jsr BCD
	sta cia1base+10
	ldx #6
	jsr BCD
	sta cia1base+9
	LoadB cia1base+8, 0     ; writing the tenths starts the clock
	rts

; two digits at timeText+X: BCD in A
BCD:
	lda timeText,x
	asl
	asl
	asl
	asl
	sta r0L
	lda timeText+1,x
	and #$0f
	ora r0L
	rts

; two digits at timeText+X: value in A
Number:
	lda timeText,x
	and #$0f
	asl
	sta r0L
	asl
	asl
	adc r0L
	sta r0L
	lda timeText+1,x
	and #$0f
	adc r0L
	rts

request:
	.byte "R", WIC64_GET_LOCAL_TIME, 0, 0
timeText:
	.res TIME_LENGTH

; Sets ramExpSize to the number of 64 KB REU banks (0 = no REU), at most
; REU_MAX_BANKS: writes a marker to every bank tested and stops when a bank
; does not keep it, or when bank 0 changes (wrap-around of a smaller REU).
DetectREU:
	ldy #0
	sty ramExpSize
	; present? two DMA round trips through bank 0 must work: an open I/O
	; area can echo register writes, so test the DMA itself
	lda #$a5
@test:	sta reuMark+1
	lda #0
	jsr @stashv
	lda #0
	sta reuMark
	jsr @fetch
	cmp reuMark+1
	bne @end
	eor #$ff
	cmp #$5a
	beq @test
	jsr @stash
@next:	iny
	cpy #REU_MAX_BANKS
	beq @size
	jsr @stash              ; bank Y := Y
	lda #0
	jsr @fetch              ; bank 0 still 0?
	bne @size
	tya
	jsr @fetch              ; bank Y kept its marker?
	cmp reuMark+1
	beq @next
@size:	sty ramExpSize
@end:	rts

@stash:	sty reuMark+1
	tya
@stashv:
	ldx #$90                ; stash, execute now
	bne @op
@fetch:	ldx #$91                ; fetch, execute now
@op:	sta EXP_BASE+6
	LoadW EXP_BASE+2, reuMark+1
	lda #0
	sta EXP_BASE+4
	sta EXP_BASE+5
	sta EXP_BASE+8
	sta EXP_BASE+10
	LoadB EXP_BASE+7, 1
	cpx #$91
	bne @go
	LoadW EXP_BASE+2, reuMark
@go:	stx EXP_BASE+1
	lda reuMark
	rts

reuMark:
	.byte 0, 0

.endif
