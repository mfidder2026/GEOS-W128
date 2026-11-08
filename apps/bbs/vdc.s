; GEOS-W128 BBS: the VDC as a colour text terminal, the keyboard, and the CRC
;
; The terminal leaves the GEOS bitmap and uses the VDC in text mode, with the
; character ROM copied to VDC memory: screen $0000, attributes $0800, font
; $2000. That fits a 16 KB VDC. vdc_leave restores the registers; the
; application then redraws its bitmap screen.
;
; In 40 column GEOS (graphMode bit 7 clear) the same calls drive the VIC in
; text mode instead: screen $8C00 (the GEOS colour matrix), colour RAM $D800,
; and the character ROM copied to the bitmap at $A000 (upper case and
; graphics) and $A800 (lower case). VDC addresses map over: below $0800 the
; screen, from $0800 the attributes, whose RGBI colour becomes a VIC colour.
; The VIC has no hardware cursor, so the cursor is the cell shown reversed.

	.export _vdc_enter, _vdc_leave, _vdc_putc, _vdc_fill, _vdc_copy
	.export _vdc_cursor, _vdc_case, _vdc_font
	.export _kbd_scan, _kbdMods, _frames, _frame_tick, _crc16
	.import popa, popax
	.importzp ptr1, ptr2, ptr3, ptr4

config	= $ff00
vdcreg	= $d600
vdcdata	= $d601
keyext	= $d02f
cia1a	= $dc00
cia1b	= $dc01
graphMode = $3f
screencolors = $851e
vicscr	= $8c00
viccol	= $d800
vicfont	= $a000

	.bss
saved:	.res 20
tmpb:	.res 8
_kbdMods: .res 1
_frames: .res 1
wide:	.res 1
val:	.res 1
hi:	.res 1
lo:	.res 1
idle:	.res 1
row:	.res 1
found:	.res 1
crclo:	.res 1
crchi:	.res 1
cfg:	.res 1
vsave:	.res 6
vlower:	.res 1
con:	.res 1
cpos:	.res 2

	.rodata
regs:	.byte 0,1,2,3,10,11,12,13,14,15,20,21,22,24,25,26,27,28,34,35
rowbit:	.byte $fe,$fd,$fb,$f7,$ef,$df,$bf,$7f
vregs:	.byte $11,$16,$18,$20,$21,$15
; VDC RGBI -> VIC colour (the reverse of the table in term.c)
vcolour: .byte 0,11,6,14,5,13,12,3,2,10,8,4,9,7,15,1

	.code

; X = register, A = value
vset:	stx vdcreg
@w:	bit vdcreg
	bpl @w
	sta vdcdata
	rts

; X = register, out A
vget:	stx vdcreg
@w:	bit vdcreg
	bpl @w
	lda vdcdata
	rts

; hi/lo -> update address
vaddr:	ldx #18
	lda hi
	jsr vset
	inx
	lda lo
	jmp vset

; void __fastcall__ vdc_enter(unsigned char columns)   40 or 80
_vdc_enter:
	bit graphMode
	bmi :+
	jmp vic_enter
:
	sta wide
	php
	sei
	ldy #19
@s:	ldx regs,y
	jsr vget
	sta saved,y
	dey
	bpl @s
	lda #0                  ; black on black while the font loads
	ldx #26
	jsr vset
	ldx #25
	jsr vget
	and #$0f
	ldx #25
	jsr vset
	lda #0                  ; screen $0000, attributes $0800, no scrolling
	ldx #12
	jsr vset
	ldx #13
	jsr vset
	ldx #20
	lda #$08
	jsr vset
	ldx #21
	lda #0
	jsr vset
	ldx #27
	lda #0
	jsr vset
	ldx #28                 ; font $2000; bit 4 is the RAM type
	jsr vget
	and #$10
	ora #$20
	ldx #28
	jsr vset
	ldx #24                 ; no reverse, no copy
	jsr vget
	and #$1f
	ldx #24
	jsr vset
	lda wide
	cmp #40
	bne @c80
	ldx #0                  ; 40 columns of double width pixels
	lda #63
	jsr vset
	ldx #1
	lda #40
	jsr vset
	ldx #2
	lda #55
	jsr vset
	ldx #3
	lda #$45
	jsr vset
	ldx #22
	lda #$89
	jsr vset
	ldx #34
	lda #63
	jsr vset
	ldx #35
	lda #52
	jsr vset
@c80:	plp
	rts

; after the font is loaded: text mode with attributes, blinking cursor
vdc_show:
	ldx #25
	jsr vget
	and #$0f
	ora #$40
	ldy wide
	cpy #40
	bne @1
	ora #$10                ; double pixels
@1:	ldx #25
	jsr vset
	ldx #10
	lda #$60
	jsr vset
	ldx #11
	lda #7
	jmp vset

; void vdc_leave(void)
_vdc_leave:
	bit graphMode
	bmi :+
	jmp vic_leave
:
	php
	sei
	ldy #19
@r:	ldx regs,y
	lda saved,y
	jsr vset
	dey
	bpl @r
	plp
	rts

; void vdc_font(void): the character ROM to VDC memory, then show the screen
; (with $4f the ROM is at $d000, with bank 1 RAM below it)
_vdc_font:
	bit graphMode
	bmi :+
	jmp vic_font
:
	lda #$20
	sta hi
	lda #0
	sta lo
	jsr vaddr
	lda #<$d000
	sta ptr1
	lda #>$d000
	sta ptr1+1
@char:	php
	sei
	lda config
	sta cfg
	lda #$4f
	sta config
	ldy #7
@g:	lda (ptr1),y
	sta tmpb,y
	dey
	bpl @g
	lda cfg
	sta config
	plp
	ldy #0
@p:	lda tmpb,y
	ldx #31
	jsr vset
	iny
	cpy #8
	bne @p
	ldy #8
	lda #0
@z:	ldx #31
	jsr vset
	dey
	bne @z
	lda ptr1
	clc
	adc #8
	sta ptr1
	bcc @char
	inc ptr1+1
	lda ptr1+1
	cmp #$e0
	bne @char
	jmp vdc_show

; void __fastcall__ vdc_putc(unsigned addr, char c, char attr)
_vdc_putc:
	bit graphMode
	bmi :+
	jmp vic_putc
:
	sta val
	jsr popa
	pha
	jsr popax
	sta lo
	stx hi
	jsr vaddr
	pla
	ldx #31
	jsr vset
	lda hi
	clc
	adc #8
	sta hi
	jsr vaddr
	lda val
	ldx #31
	jmp vset

; void __fastcall__ vdc_cursor(unsigned addr)
_vdc_cursor:
	bit graphMode
	bmi :+
	jmp vic_cursor
:
	pha
	txa
	ldx #14
	jsr vset
	pla
	ldx #15
	jmp vset

; void __fastcall__ vdc_fill(unsigned addr, char value, unsigned n)
; R31 writes one byte, R30 = how many more
_vdc_fill:
	bit graphMode
	bmi :+
	jmp vic_fill
:
	sta ptr1
	stx ptr1+1
	jsr popa
	sta val
	jsr popax
	sta lo
	stx hi
	ldx #24                 ; block write
	jsr vget
	and #$7f
	ldx #24
	jsr vset
	jsr vaddr
@next:	lda ptr1+1
	bne @big
	lda ptr1
	beq @done
	tay                     ; all that is left
	lda #0
	sta ptr1
	beq @go
@big:	ldy #255
	lda ptr1
	sec
	sbc #255
	sta ptr1
	bcs @go
	dec ptr1+1
@go:	lda val
	ldx #31
	jsr vset
	dey
	beq @next
	tya
	ldx #30
	jsr vset
	jmp @next
@done:	rts

; void __fastcall__ vdc_copy(unsigned dst, unsigned src, unsigned n)
; R30 = bytes to copy, 0 = 256; both addresses move on by themselves
_vdc_copy:
	bit graphMode
	bmi :+
	jmp vic_copy
:
	sta ptr1
	stx ptr1+1
	jsr popax
	sta ptr2
	stx ptr2+1
	jsr popax
	sta lo
	stx hi
	ldx #24
	jsr vget
	ora #$80
	ldx #24
	jsr vset
	jsr vaddr
	lda ptr2+1
	ldx #32
	jsr vset
	lda ptr2
	ldx #33
	jsr vset
@next:	lda ptr1+1
	beq @last
	dec ptr1+1
	lda #0
	beq @go
@last:	lda ptr1
	beq @done
	ldy #0
	sty ptr1
@go:	ldx #30
	jsr vset
	jmp @next
@done:	ldx #24
	jsr vget
	and #$7f
	ldx #24
	jmp vset

; void __fastcall__ vdc_case(unsigned char lower, unsigned cells)
; set or clear the alternate font bit of all attributes: the C64 changes the
; case of the whole screen at once, so does this
_vdc_case:
	bit graphMode
	bmi :+
	jmp vic_case
:
	sta ptr1
	stx ptr1+1
	jsr popa
	sta val
	lda #$08
	sta hi
	lda #0
	sta lo
@next:	lda ptr1
	ora ptr1+1
	beq @done
	jsr vaddr
	ldx #31
	jsr vget
	and #$7f
	ldy val
	beq @1
	ora #$80
@1:	pha
	jsr vaddr
	pla
	ldx #31
	jsr vset
	inc lo
	bne @2
	inc hi
@2:	lda ptr1
	bne @3
	dec ptr1+1
@3:	dec ptr1
	jmp @next
@done:	rts

;------------------------------------------------------------- the VIC ----

; A/X = VDC address -> A/X = VIC address; carry set for an attribute
vmap:	cpx #$08
	bcs @a
	pha
	txa
	clc
	adc #>vicscr
	tax
	pla
	clc
	rts
@a:	pha
	txa
	clc
	adc #(>viccol) - 8
	tax
	pla
	sec
	rts

; A = attribute -> VIC colour; keeps Y
vxlat:	and #$0f
	tax
	lda vcolour,x
	rts

; the cursor cell back to normal
cur_off:
	lda con
	beq @d
	lda cpos
	sta ptr4
	lda cpos+1
	sta ptr4+1
	ldy #0
	lda (ptr4),y
	eor #$80
	sta (ptr4),y
	sty con
@d:	rts

vic_enter:
	php
	sei
	ldy #5
@s:	ldx vregs,y
	lda $d000,x
	sta vsave,y
	dey
	bpl @s
	lda #0
	sta $d015               ; no sprites
	sta $d020
	sta $d021
	sta con
	lda vsave               ; screen off while the font loads
	and #$07
	ora #$08
	sta $d011
	lda vsave+1             ; no multicolour
	and #$ef
	sta $d016
	lda #1
	sta vlower
	plp
	rts

vic_leave:
	php
	sei
	ldy #5
@r:	ldx vregs,y
	lda vsave,y
	sta $d000,x
	dey
	bpl @r
	plp
	lda #<vicscr            ; the GEOS colours back in the colour matrix
	sta ptr4
	lda #>vicscr
	sta ptr4+1
	lda #0
	sta con
	ldx #<1000
	lda #>1000
	sta ptr1+1
	lda screencolors
	jmp vfill

; the character ROM to $A000-$AFFF, eight bytes at a time ($4f shows the ROM
; at $d000 over bank 1, so each group goes through tmpb)
vic_font:
	lda #<vicfont
	sta ptr2
	lda #>vicfont
	sta ptr2+1
	lda #<$d000
	sta ptr1
	lda #>$d000
	sta ptr1+1
@char:	php
	sei
	lda config
	sta cfg
	lda #$4f
	sta config
	ldy #7
@g:	lda (ptr1),y
	sta tmpb,y
	dey
	bpl @g
	lda cfg
	sta config
	plp
	ldy #7
@p:	lda tmpb,y
	sta (ptr2),y
	dey
	bpl @p
	lda ptr2
	clc
	adc #8
	sta ptr2
	bcc @n
	inc ptr2+1
@n:	lda ptr1
	clc
	adc #8
	sta ptr1
	bcc @char
	inc ptr1+1
	lda ptr1+1
	cmp #$e0
	bne @char
vshow:	lda vlower
	beq @u
	lda #$3a                ; screen $8c00, font $a800
	.byte $2c
@u:	lda #$38                ; font $a000
	sta $d018
	lda vsave
	and #$07
	ora #$18                ; text mode, screen on
	sta $d011
	rts

; void __fastcall__ vdc_putc(unsigned addr, char c, char attr)
vic_putc:
	sta val
	jsr popa
	sta tmpb
	jsr popax
	sta lo
	stx hi
	jsr cur_off
	lda lo
	ldx hi
	jsr vmap
	sta ptr4
	stx ptr4+1
	ldy #0
	lda tmpb
	sta (ptr4),y
	lda ptr4+1
	clc
	adc #(>viccol) - (>vicscr)
	sta ptr4+1
	lda val
	jsr vxlat
	ldy #0
	sta (ptr4),y
	rts

; void __fastcall__ vdc_cursor(unsigned addr)
vic_cursor:
	pha
	txa
	pha
	jsr cur_off
	pla
	tax
	pla
	jsr vmap
	sta cpos
	stx cpos+1
	sta ptr4
	stx ptr4+1
	ldy #0
	lda (ptr4),y
	eor #$80
	sta (ptr4),y
	lda #1
	sta con
	rts

; void __fastcall__ vdc_fill(unsigned addr, char value, unsigned n)
vic_fill:
	sta ptr1
	stx ptr1+1
	jsr popa
	sta val
	jsr popax
	sta lo
	stx hi
	jsr cur_off
	lda lo
	ldx hi
	jsr vmap
	sta ptr4
	stx ptr4+1
	lda val
	bcc @s
	jsr vxlat
@s:	ldx ptr1
; A = value, X = count low, ptr1+1 = count high, ptr4 = where
vfill:	ldy #0
	stx ptr1
	ldx ptr1+1
	beq @rest
@page:	sta (ptr4),y
	iny
	bne @page
	inc ptr4+1
	dex
	bne @page
@rest:	ldx ptr1
	beq @d
@b:	sta (ptr4),y
	iny
	dex
	bne @b
@d:	rts

; void __fastcall__ vdc_copy(unsigned dst, unsigned src, unsigned n)
; upwards only, which is how the terminal moves cells
vic_copy:
	sta ptr1
	stx ptr1+1
	jsr popax
	sta ptr3
	stx ptr3+1
	jsr popax
	sta lo
	stx hi
	jsr cur_off
	lda ptr3
	ldx ptr3+1
	jsr vmap
	sta ptr3
	stx ptr3+1
	lda lo
	ldx hi
	jsr vmap
	sta ptr4
	stx ptr4+1
	ldy #0
	ldx ptr1+1
	beq @rest
@page:	lda (ptr3),y
	sta (ptr4),y
	iny
	bne @page
	inc ptr3+1
	inc ptr4+1
	dex
	bne @page
@rest:	ldx ptr1
	beq @d
@b:	lda (ptr3),y
	sta (ptr4),y
	iny
	dex
	bne @b
@d:	rts

; void __fastcall__ vdc_case(unsigned char lower, unsigned cells)
; the VIC shows one font for the whole screen
vic_case:
	jsr popa
	sta vlower
	jmp vshow

; unsigned char kbd_scan(void): key 0-87 (row * 8 + column), $ff = none;
; rows 8-10 are the C128 extra keys. kbdMods: 1 shift, 2 C=, 4 CTRL.
; Interrupts stay off: GEOS scans the same lines from its interrupt.
_kbd_scan:
	php
	sei
	lda cia1a
	pha
	lda #$ff
	sta found
	sta cia1a
	sta keyext
	lda cia1b               ; a joystick in port 1 pulls these down too
	sta idle
	lda #0
	sta _kbdMods
	sta row
@row:	lda row
	cmp #8
	bcs @ext
	tax
	lda rowbit,x
	sta cia1a
	jmp @read
@ext:	sec
	sbc #8
	tax
	lda #$ff
	sta cia1a
	lda rowbit,x
	sta keyext
@read:	lda cia1b
	eor #$ff
	and idle
	ldx row
	cpx #1                  ; take out the modifiers
	bne @r6
	tay
	and #$80
	beq @n1
	lda #1
	ora _kbdMods
	sta _kbdMods
@n1:	tya
	and #$7f
	jmp @got
@r6:	cpx #6
	bne @r7
	tay
	and #$10
	beq @n6
	lda #1
	ora _kbdMods
	sta _kbdMods
@n6:	tya
	and #$ef
	jmp @got
@r7:	cpx #7
	bne @got
	tay
	and #$20
	beq @n7
	lda #2
	ora _kbdMods
	sta _kbdMods
@n7:	tya
	and #$04
	beq @n8
	lda #4
	ora _kbdMods
	sta _kbdMods
@n8:	tya
	and #$db
@got:	cmp #0
	beq @nx
	ldx found               ; the first key found counts
	cpx #$ff
	bne @nx
	ldy #0
@bit:	lsr
	bcs @have
	iny
	bne @bit
@have:	lda row
	asl
	asl
	asl
	sty found
	ora found
	sta found
@nx:	lda #$ff
	sta keyext
	inc row
	lda row
	cmp #11
	beq @end
	jmp @row
@end:	pla
	sta cia1a
	plp
	lda found
	ldx #0
	rts

; called from the GEOS interrupt, once per frame
_frame_tick:
	inc _frames
	rts

; unsigned __fastcall__ crc16(const char *p, unsigned n)   XMODEM, poly $1021
_crc16:
	sta ptr2
	stx ptr2+1
	jsr popax
	sta ptr1
	stx ptr1+1
	lda #0
	sta crclo
	sta crchi
	tay
@byte:	lda ptr2
	ora ptr2+1
	beq @end
	lda (ptr1),y
	eor crchi
	sta crchi
	ldx #8
@b:	asl crclo
	rol crchi
	bcc @n
	lda crchi
	eor #$10
	sta crchi
	lda crclo
	eor #$21
	sta crclo
@n:	dex
	bne @b
	inc ptr1
	bne @1
	inc ptr1+1
@1:	lda ptr2
	bne @2
	dec ptr2+1
@2:	dec ptr2
	jmp @byte
@end:	lda crclo
	ldx crchi
	rts
