#include "csv_test.h"
#include "jpg_test.h"
#include "mo_test.h"
#include "png_suite_test.h"

int main() {
    png_suite_test("PngSuite", "gen");
    jpg_test("jpg", "gen");
    csv_test("csv");
    gettext_test("gettext");
    return 0;
}
