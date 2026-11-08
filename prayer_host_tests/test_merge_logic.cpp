/* Host test for merge_logic_jamat_resolve() -- the pure decision rule
   extracted from svc_prayer_engine_service.c (#138 item (l)).
   Pins the exact boundary semantics: file == adhan is ACCEPTED
   (adhan_sec is the START of the window, not an upper bound), and
   file == next is ABOVE_NEXT (mosque authority overrides the bracket,
   with a warn). Both boundaries are what the pre-#138 merge_jamat()
   enforced; this test characterizes that rule for the extracted code. */
#include <stdio.h>
#include "merge_logic.h"

static int bad = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); bad++; } } while (0)

int main() {
    merge_jamat_action_t act;
    uint32_t r;

    /* !has_file -> computed, status NO_FILE */
    r = merge_logic_jamat_resolve(false, 9999, 1000, 500, 2000, &act);
    CHECK(r == 1000, "!has_file returns computed_val");
    CHECK(act == MERGE_JAMAT_NO_FILE, "!has_file status NO_FILE");

    /* file < adhan -> computed (reject), status BELOW_ADHAN */
    r = merge_logic_jamat_resolve(true, 400, 1000, 500, 2000, &act);
    CHECK(r == 1000, "file < adhan returns computed_val (reject)");
    CHECK(act == MERGE_JAMAT_BELOW_ADHAN, "file < adhan status BELOW_ADHAN");

    /* boundary: file == adhan -> file (accept), status ACCEPTED.
       adhan_sec is the START of the prayer window; file == adhan means
       the mosque starts jamat exactly at adhan. Legal. */
    r = merge_logic_jamat_resolve(true, 500, 1000, 500, 2000, &act);
    CHECK(r == 500, "file == adhan accepted (window start boundary)");
    CHECK(act == MERGE_JAMAT_ACCEPTED, "file == adhan status ACCEPTED");

    /* file in (adhan, next) -> file, status ACCEPTED */
    r = merge_logic_jamat_resolve(true, 1500, 1000, 500, 2000, &act);
    CHECK(r == 1500, "file in (adhan, next) accepted");
    CHECK(act == MERGE_JAMAT_ACCEPTED, "file in (adhan, next) status ACCEPTED");

    /* boundary: file == next -> file (accept with warn), status ABOVE_NEXT.
       file >= next triggers the mosque-authority override. */
    r = merge_logic_jamat_resolve(true, 2000, 1000, 500, 2000, &act);
    CHECK(r == 2000, "file == next accepted with warn (upper boundary)");
    CHECK(act == MERGE_JAMAT_ABOVE_NEXT, "file == next status ABOVE_NEXT");

    /* file > next -> file, status ABOVE_NEXT */
    r = merge_logic_jamat_resolve(true, 3000, 1000, 500, 2000, &act);
    CHECK(r == 3000, "file > next accepted with warn");
    CHECK(act == MERGE_JAMAT_ABOVE_NEXT, "file > next status ABOVE_NEXT");

    /* next_val == 0 sentinel: no upper bracket (e.g. Isha with no
       tomorrow rollover), always ACCEPTED silently if file >= adhan. */
    r = merge_logic_jamat_resolve(true, 3000, 1000, 500, 0, &act);
    CHECK(r == 3000, "next_val==0 (no bracket) accepted silently");
    CHECK(act == MERGE_JAMAT_ACCEPTED, "next_val==0 status ACCEPTED");

    /* NULL action pointer is safe (caller may not care about status) */
    r = merge_logic_jamat_resolve(true, 1500, 1000, 500, 2000, NULL);
    CHECK(r == 1500, "NULL action pointer works");

    printf("=> %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
