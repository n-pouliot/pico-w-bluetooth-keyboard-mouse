#include <stdio.h>

int test_hid_report_parser(void);
int test_pairing_store(void);
int test_bridge_mailbox(void);
int test_ble_bridge_policy(void);

int main(void)
{
    int failures = 0;

    failures += test_hid_report_parser();
    failures += test_pairing_store();
    failures += test_bridge_mailbox();
    failures += test_ble_bridge_policy();
    if (failures != 0) {
        (void)fprintf(stderr, "Gate 1 host tests failed: %d suite(s)\n",
                      failures);
        return 1;
    }
    (void)printf("Gate 1 host tests passed\n");
    return 0;
}
