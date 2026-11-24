/* Open gate on the camera: the only header other code includes.
 *
 * One operation brings camera memory to a mode. Addresses, bytes, memory targets, the NTSC/PAL family and undo
 * stay inside native/open-gate; the site data is generated at build time from the open-gate designs.
 */
#ifndef OG_H
#define OG_H

typedef enum { OG_OFF = 0, OG_4K_32 = 1, OG_57K_32 = 2, OG_MODE_COUNT } og_mode;

typedef enum {
    OG_OK = 0,
    OG_BUSY,          /* another reconcile is running */
    OG_NOT_READY,     /* the coprocessor or the stored record is not reachable yet */
    OG_WRONG_DIAL,    /* not in stills */
    OG_FOREIGN,       /* a site holds neither stock nor any preset's bytes: nothing written */
    OG_IO,            /* a read or write failed and the touched sites were put back */
    OG_UNDO_FAILED,   /* a write failed and putting back failed too: cold boot needed */
    OG_NOT_SAVED      /* memory is in the mode, but /setting/open_gate.dat could not be written */
} og_status;

/* Shooting-screen hook: bring memory to the saved mode. Also runs after a short power-off. */
void og_power_on(void);

/* Menu confirm: stills only; reconcile to mode, then save it. */
og_status og_select(og_mode mode);

/* Menu getter: the saved mode. */
og_mode og_selected(void);

#endif
