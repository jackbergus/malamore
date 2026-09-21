
#include <yaucl/data/csv.h>

// Global, statically allocated buffer shared by all tables
constexpr size_t STATIC_BUFFER_SIZE = 100000;
double g_static_buffer[STATIC_BUFFER_SIZE];
size_t g_buffer_offset = 0; // Tracks the next free slot

// Parses a CSV file into the shared static buffer
bool parse_csv_to_static_buffer(const std::string& filename, StaticTable& out_table) {
    out_table.data = nullptr;
    out_table.rows = 0;
    out_table.cols = 0;
    if (g_buffer_offset >= STATIC_BUFFER_SIZE) {
        std::cerr << "Error: cannot allocate anymore data on the static buffer" << std::endl;
        return false;
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << "\n";
        return false;
    }

    out_table.data = &g_static_buffer[g_buffer_offset];
    std::string line;
    size_t total_elements = 0;

    while (std::getline(file, line)) {
        std::string_view view(line);
        size_t current_row_cols = 0;

        while (!view.empty()) {
            if (g_buffer_offset + total_elements >= STATIC_BUFFER_SIZE) {
                std::cerr << "Error: Shared static buffer out of memory!\n";
                return false;
            }

            size_t comma_pos = view.find(',');
            std::string_view token = (comma_pos == std::string_view::npos) ? view : view.substr(0, comma_pos);

            double value = 0.0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);

            if (ec == std::errc()) {
                out_table.data[total_elements] = value;
                total_elements++;
                current_row_cols++;
            }

            if (comma_pos == std::string_view::npos) {
                break;
            }
            view.remove_prefix(comma_pos + 1);
        }

        if (current_row_cols > 0) {
            if (out_table.rows == 0) {
                out_table.cols = current_row_cols;
            }
            out_table.rows++;
        }
    }

    g_buffer_offset += total_elements;
    return true;
}