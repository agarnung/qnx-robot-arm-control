/*
 * test_urscript.c — pins the exact URScript strings produced by
 * vcr_build_urscript(). The strings are part of the wire contract with the
 * UR controller, so a formatting slip (wrong precision, wrong argument order,
 * missing NUL room) must fail here instead of on a robot.
 */
#include "urscript.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

static int contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

int main(void) {
    vcr_cmd_msg_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.magic = VCR_MAGIC;
    cmd.version = VCR_PROTO_VERSION;
    cmd.type = VCR_CMD_MOTION;
    cmd.velocity = 0.25;
    cmd.acceleration = 1.2;
    for (int i = 0; i < 6; i++) {
        cmd.joints[i] = 0.1 * (double)(i + 1);
    }

    char buf[512];

    cmd.mode = VCR_MODE_MOVEJ;
    int n = vcr_build_urscript(&cmd, buf, (int)sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "FAIL: movej build returned %d\n", n);
        return 1;
    }
    const char *expected_movej =
        "movej([0.100000, 0.200000, 0.300000, 0.400000, 0.500000, 0.600000], "
        "v=0.2500, a=1.2000)";
    if (!contains(buf, expected_movej)) {
        fprintf(stderr, "FAIL: movej script = %s\n", buf);
        return 1;
    }

    cmd.mode = VCR_MODE_SERVOJ;
    cmd.servoj_t = 0.008;
    cmd.servoj_lookahead = 0.1;
    cmd.servoj_gain = 300;
    n = vcr_build_urscript(&cmd, buf, (int)sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "FAIL: servoj build returned %d\n", n);
        return 1;
    }
    const char *expected_servoj =
        "servoj([0.100000, 0.200000, 0.300000, 0.400000, 0.500000, 0.600000], "
        "a=1.2000, v=0.2500, t=0.0080, lookahead_time=0.1000, gain=300)";
    if (!contains(buf, expected_servoj)) {
        fprintf(stderr, "FAIL: servoj script = %s\n", buf);
        return 1;
    }

    cmd.mode = VCR_MODE_HOME_PROBE;
    n = vcr_build_urscript(&cmd, buf, (int)sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "FAIL: home probe build returned %d\n", n);
        return 1;
    }
    const char *expected_home =
        "servoj(get_joint_positions(), a=1.2000, v=0.2500, t=0.0080, "
        "lookahead_time=0.1000, gain=300)";
    if (!contains(buf, expected_home)) {
        fprintf(stderr, "FAIL: home probe script = %s\n", buf);
        return 1;
    }

    char tiny[8];
    if (vcr_build_urscript(&cmd, tiny, (int)sizeof(tiny)) != -1) {
        fprintf(stderr, "FAIL: build accepted a too-small buffer\n");
        return 1;
    }

    cmd.mode = 99;
    n = vcr_build_urscript(&cmd, buf, (int)sizeof(buf));
    if (n <= 0 || !contains(buf, "movej(")) {
        fprintf(stderr, "FAIL: unknown mode did not fall back to movej\n");
        return 1;
    }

    printf("OK: test_urscript (movej, servoj, home probe, small buffer)\n");
    return 0;
}
