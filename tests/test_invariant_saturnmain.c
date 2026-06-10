#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

/* Invariant: The HPSDR endpoint must reject unauthenticated UDP control packets
 * and must NOT silently accept hardware configuration commands from unknown sources.
 * Any packet that triggers SetMicLineInput/SetBalancedMicInput without auth is a violation.
 */

#define HPSDR_PORT 1024
#define RECV_TIMEOUT_SEC 1

/* Payloads: exact exploit byte (mic line input bit set), boundary (0x00), valid-looking (0x20) */
static const unsigned char payloads[][64] = {
    { 0xEF, 0xFE, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01 }, /* exact exploit: mic line input bit set */
    { 0xEF, 0xFE, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* boundary: all zero control byte */
    { 0xEF, 0xFE, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x20 }, /* balanced mic input bit set */
};
static const size_t payload_sizes[] = { 9, 9, 9 };
static const int num_payloads = 3;

START_TEST(test_unauthenticated_hpsdr_rejected)
{
    /* Invariant: unauthenticated UDP packets must not be silently accepted;
     * the protocol MUST require authentication or reject unknown sources. */
    for (int i = 0; i < num_payloads; i++) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        ck_assert_msg(sock >= 0, "Failed to create socket");

        struct timeval tv = { .tv_sec = RECV_TIMEOUT_SEC, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port   = htons(HPSDR_PORT),
            .sin_addr.s_addr = inet_addr("127.0.0.1"),
        };

        ssize_t sent = sendto(sock, payloads[i], payload_sizes[i], 0,
                              (struct sockaddr *)&dest, sizeof(dest));
        ck_assert_msg(sent == (ssize_t)payload_sizes[i], "sendto failed");

        /* Expect an auth-rejection response (non-zero reply indicating refusal),
         * OR no response at all (packet silently dropped by auth layer).
         * A response that echoes acceptance without auth token is a FAIL. */
        unsigned char resp[256] = {0};
        ssize_t n = recv(sock, resp, sizeof(resp), 0);

        if (n > 0) {
            /* If a response is received, it must NOT be an acceptance/ack (0xEF 0xFE 0x02/0x03).
             * Acceptance codes 0x02 and 0x03 indicate the server processed the command. */
            int is_acceptance = (n >= 3 && resp[0] == 0xEF && resp[1] == 0xFE &&
                                 (resp[2] == 0x02 || resp[2] == 0x03));
            ck_assert_msg(!is_acceptance,
                "Payload %d: unauthenticated packet was ACCEPTED (CWE-287 violation)", i);
        }
        /* n <= 0 (timeout/EAGAIN) means packet was dropped — acceptable auth behavior */

        close(sock);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s = suite_create("Security");
    TCase *tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 10);
    tcase_add_test(tc_core, test_unauthenticated_hpsdr_rejected);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void)
{
    int number_failed;
    Suite *s = security_suite();
    SRunner *sr = srunner_create(s);
    srunner_