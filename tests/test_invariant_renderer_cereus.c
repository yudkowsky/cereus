#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declare the vulnerable function from renderer_cereus.c */
extern void renderer_cache_asset(const char *path);
extern int get_asset_cache_count(void);
extern const char* get_cached_asset_path(int index);

START_TEST(test_buffer_read_bounds_asset_cache)
{
    /* Invariant: Buffer reads never exceed declared length in asset_cache */
    const char *payloads[] = {
        "valid_short_path.txt",                                    /* Valid input */
        "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y.txt", /* Boundary: ~60 chars */
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", /* Exploit: 300+ chars */
        "x" /* Minimal valid */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        int initial_count = get_asset_cache_count();
        
        /* Call the vulnerable function with oversized input */
        renderer_cache_asset(payloads[i]);
        
        int final_count = get_asset_cache_count();
        
        /* Invariant: Either the function rejected the input or safely truncated it */
        if (final_count > initial_count) {
            const char *cached = get_cached_asset_path(final_count - 1);
            ck_assert_ptr_nonnull(cached);
            
            /* Verify cached path does not exceed reasonable buffer bounds (256 bytes typical) */
            size_t cached_len = strlen(cached);
            ck_assert_int_le(cached_len, 256);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_read_bounds_asset_cache);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}