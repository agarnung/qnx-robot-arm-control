/* urscript.h — build the URScript text line for a motion command.
 * Mirrors src/control/arm.py's build_movej()/build_servoj() exactly so the
 * two components (Python vision side, historically; C control_core now)
 * produce byte-identical scripts for the same MotionCommand. */
#ifndef VCR_URSCRIPT_H
#define VCR_URSCRIPT_H

#include "protocol.h"

/* Writes a NUL-terminated URScript command into buf (size bytes). Returns
 * the number of bytes written (excluding NUL), or -1 if buf was too small. */
int vcr_build_urscript(const vcr_cmd_msg_t *cmd, char *buf, int size);

#endif
