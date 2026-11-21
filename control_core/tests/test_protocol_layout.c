/*
 * test_protocol_layout.c — pins sizeof(vcr_cmd_msg_t)/sizeof(vcr_status_msg_t)
 * at exactly 108 bytes each. This is the single source of truth that
 * python_client/control_client.py's struct.calcsize() is checked against at
 * import time — if someone edits protocol.h without updating the Python
 * side, `make test` fails here LONG before a confusing runtime EPROTO from
 * ipc_posix.c (magic/version mismatch is checked there, but a silent size
 * drift with the same magic/version would not be).
 */
#include "protocol.h"
#include <stdio.h>

int main(void) {
    int fail = 0;

    if (sizeof(vcr_cmd_msg_t) != 108) {
        fprintf(stderr, "FAIL: sizeof(vcr_cmd_msg_t) = %zu, expected 108\n",
                sizeof(vcr_cmd_msg_t));
        fail = 1;
    }
    if (sizeof(vcr_status_msg_t) != 108) {
        fprintf(stderr, "FAIL: sizeof(vcr_status_msg_t) = %zu, expected 108\n",
                sizeof(vcr_status_msg_t));
        fail = 1;
    }

    if (!fail) {
        printf("OK: test_protocol_layout (cmd=%zu status=%zu bytes)\n",
               sizeof(vcr_cmd_msg_t), sizeof(vcr_status_msg_t));
    }
    return fail;
}
