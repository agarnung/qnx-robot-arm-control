#include "urscript.h"
#include <stdio.h>

int vcr_build_urscript(const vcr_cmd_msg_t *cmd, char *buf, int size) {
    char joints[160];
    int n = snprintf(joints, sizeof(joints),
        "[%.6f, %.6f, %.6f, %.6f, %.6f, %.6f]",
        cmd->joints[0], cmd->joints[1], cmd->joints[2],
        cmd->joints[3], cmd->joints[4], cmd->joints[5]);
    if (n < 0 || (size_t)n >= sizeof(joints)) return -1;

    int written;
    if (cmd->mode == VCR_MODE_HOME_PROBE) {
        /* cmd->joints intentionally unused: get_joint_positions() is
         * evaluated on the controller itself. */
        written = snprintf(buf, (size_t)size,
            "servoj(get_joint_positions(), a=%.4f, v=%.4f, t=%.4f, "
            "lookahead_time=%.4f, gain=%u)",
            cmd->acceleration, cmd->velocity,
            cmd->servoj_t, cmd->servoj_lookahead, cmd->servoj_gain);
    } else if (cmd->mode == VCR_MODE_SERVOJ) {
        written = snprintf(buf, (size_t)size,
            "servoj(%s, a=%.4f, v=%.4f, t=%.4f, lookahead_time=%.4f, gain=%u)",
            joints, cmd->acceleration, cmd->velocity,
            cmd->servoj_t, cmd->servoj_lookahead, cmd->servoj_gain);
    } else {
        written = snprintf(buf, (size_t)size,
            "movej(%s, v=%.4f, a=%.4f)",
            joints, cmd->velocity, cmd->acceleration);
    }
    if (written < 0 || written >= size) return -1;
    return written;
}
