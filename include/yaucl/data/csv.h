// Author Giacomo Bergami, PhD

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <charconv>
#include <ostream>
#include <system_error>
#include <span> // Required for std::span


// Represents a parsed table view inside the static buffer, where a double matrix is represented in a row-major fashion
struct StaticTable {
    double* data = nullptr;
    size_t rows = 0;
    size_t cols = 0;

    // Returns a mutable reference to the cell at row i, column j
    double* cell(size_t i, size_t j) {
        if (i >= rows || j >= cols) {
            return nullptr;
        } else {
            return &data[i * cols + j];
        }
    }

    // Returns a read-only reference to the cell at row i, column j (const overload)
    const double* cell(size_t i, size_t j) const {
        if (i >= rows || j >= cols) {
            return nullptr;
        } else {
            return &data[i * cols + j];
        }
    }

    // Returns a lightweight, non-owning view of the i-th row
    std::span<double> operator[](size_t i) {
        return {&data[i * cols], cols};
    }

    // Returns a read-only lightweight view of the i-th row (const overload)
    std::span<const double> operator[](size_t i) const {
        return {&data[i * cols], cols};
    }

    friend std::ostream& operator<<(std::ostream &os, const StaticTable &obj) {
        for (size_t i = 0; i < obj.rows; ++i) {
            // Using the new row utility method to print
            auto row_view = obj[i];
            for (double val : row_view) {
                os << val << " ";
            }
            os << std::endl;
        }
        return os;
    }

};

// Parses a CSV file into the shared static buffer
bool parse_csv_to_static_buffer(const std::string& filename, StaticTable& out_table);