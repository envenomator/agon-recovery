    .org 40000h

source:         .EQU 50000h
destination:    .EQU 0h
length:         .EQU 70000h
ackdone:        .EQU 70003h

    LD sp, 0BFFFFh

    ; reset ack variable to false
    LD      A, 0h
    LD      (ackdone), A

    ; open flash for write access
    LD      A, B6h  ; unlock
    OUT0    (F5h), A
    LD      A, 49h
    OUT0    (F5h), A
    LD      A, 0h   ; unprotect all pages
    OUT0    (FAh), A

    LD      A, B6h  ; unlock again
    OUT0    (F5h), A
    LD      A, 49h
    OUT0    (F5h), A
    LD      A, 5Fh  ; Ceiling(18Mhz * 5,1us) = 95, or 0x5F
    OUT0    (F9h), A

    ; mass erase flash
    LD      A, 01h
    OUT0    (FFh), A
erasewait:
    IN0     A, (FFh)
    AND     A, 01h
    JR      nz, erasewait

    ; flash
    LD		DE, destination
    LD		HL, source
	LD      BC, (length)
    LDIR

    ; protect flash pages 
    LD      A, B6h  ; unlock
    OUT0    (F5h), A
    LD      A, 49h
    OUT0    (F5h), A
    LD      A, FFh  ; protect all pages
    OUT0    (FAh), A

    ; set ack variable to done
    LD      A, 1h
    LD      (ackdone), A

end:
    JR end