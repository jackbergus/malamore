//
// Created by Giacomo Bergami, PhD on 21/09/2026.
//

#include <yaucl/data/csv.h>

int main(void) {

    StaticTable table_test;
    if (parse_csv_to_static_buffer("C:\\Users\\diste\\Downloads\\malamore\\reader_csv.csv", table_test)) {
        std::cout << table_test << std::endl;
    }

    return 0;
}