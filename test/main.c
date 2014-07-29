#include "png_suite_test.h"
#include "jpg_test.h"
#include "csv_test.h"

int main() {
    const char *rgbaPath = "gen";
    png_suite_test("PngSuite", rgbaPath);
    jpg_test("jpg", rgbaPath);
    csv_test("csv");
    return 0;
}