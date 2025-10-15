// Abstract Base-Class for any additional readers.
class TSVReader {
public:
    TSVReader(const std::vector<std::string> &node_file_paths,
                         const std::vector<std::string> &edge_file_paths,
                         size_t idx_node_id, const std::vector<size_t> &idx_node_type,
                         size_t idx_start_node_id, size_t idx_end_node_id, const std::vector<size_t> &idx_edge_type);
    ~TSVReader() = default;

    m1_data readTo(GenericGraphReader &model, std::map<std::string, std::string> meta_data,
        std::mt19937_64::result_type seed, bool debug);

protected:
    std::vector<std::string> nodefiles{};
    std::vector<std::string> edgefiles{};

    size_t idx_node_id = 0;
    std::vector<size_t> idx_node_type = {1};

    size_t idx_start_node_id = 0;
    size_t idx_end_node_id = 1;
    std::vector<size_t> idx_edge_type = {2};
};


TSVReader::TSVReader(const std::vector<std::string> &node_file_paths,
                         const std::vector<std::string> &edge_file_paths,
                         const size_t idx_node_id=0, const std::vector<size_t> &idx_node_type={1},
                         const size_t idx_start_node_id=0, const size_t idx_end_node_id=1, const std::vector<size_t> &idx_edge_type={2}){
    this->nodefiles = node_file_paths;
    this->edgefiles = edge_file_paths;

    this->idx_node_id = idx_node_id;
    this->idx_node_type = idx_node_type;

    this->idx_start_node_id = idx_start_node_id;
    this->idx_end_node_id = idx_end_node_id;
    this->idx_edge_type = idx_edge_type;
}



size_t split_on_char(const std::string &s, const char delimiter, std::vector<std::string>& container) {
    size_t count = 0;
    container.clear();

    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        container.push_back(item);
        ++count;
    }
    return count;
}


m1_data TSVReader::readTo(GenericGraphReader& model, std::map<std::string, std::string> meta_data, std::mt19937_64::result_type seed, bool debug=false){
    // Read all provided Node-Files
    for (const std::string& filename : this->nodefiles) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Error opening node file '" + filename + "'.");
        }

        std::filesystem::path path{filename};
        std::cout << "\tReading '" << path.string() << "' (" << std::filesystem::file_size(path) << " byte)." << std::endl;
        long long node_count = 0;
        long long lines_skipped = 0;

        std::string line;


        // Skip first line, this defines the structure of the file.
        std::getline(file, line);
        if (line.ends_with('\r')) {
            line.erase(line.length() - 1, 1);
        }
        std::vector<std::string> columns;
        const size_t expected_nbr_of_columns = split_on_char(line, '\t', columns);

        // Check if the provided structure is compatible with the provided indices.
        if (this->idx_node_id >= columns.size()) {
            throw std::runtime_error("This file does not define enough columns to read the node-id at index "
                + std::to_string(this->idx_node_id)
                + ". Expected at least " + std::to_string(this->idx_node_id+1) + " columns, got " + std::to_string(columns.size()) + ".");
        }
        const size_t highest_idx = *std::max_element(this->idx_node_type.begin(), this->idx_node_type.end());
        if (highest_idx >= columns.size()) {
            throw std::runtime_error("This file does not define enough columns to read part of the node-type at index "
                + std::to_string(highest_idx)
                + ". Expected at least " + std::to_string(highest_idx+1) + " columns, got " + std::to_string(columns.size()) + ".");
        }

        // Confirm the indices to the user.
        std::cout << "\t\tReading the unique node-id from column '" << columns[this->idx_node_id] << "'." << std::endl;
        std::cout << "\t\tReading the node-type as a composite from columns:";
        for (auto idx: this->idx_node_type) {
            std::cout << " '" << columns[idx] << "'";
        }
        std::cout << "." << std::endl;



        while (std::getline(file, line)) {
            // Remove stray \r characters. These may appear in files created under windows (\r\n instead of just \n)
            if (line.ends_with('\r')) {
                line.erase(line.length() - 1, 1);
            }

            // Split the line on the provided delimiter.
            size_t actual_nbr_of_columns = split_on_char(line, '\t', columns);
            if (actual_nbr_of_columns != expected_nbr_of_columns) {
                ++lines_skipped;
                if (debug) {
                    std::cout << "\t\tSkipping invalid line: '" << line << "'" << std::endl;
                }
                continue;
            }

            // Read the node-ID and node-type based on the configuration. The Node-Type can be a composite from multiple columns.
            std::string node_id = columns[this->idx_node_id];
            std::string node_type = "";
            for (auto idx: this->idx_node_type) {
                node_type.append(columns[idx] + "_");
            }
            if (node_type.size() != 0) {node_type.erase(node_type.length() - 1, 1);} // Remove the trailing underscore.

            model.readNode(node_id, node_type);
            ++node_count;
        }
        file.close();
        std::cout << "\t\tRead: " << node_count << " Nodes. Skipped " << lines_skipped << " lines." << std::endl;
    }


    // Read all Edge-Files
    for (const std::string& filename : this->edgefiles) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Error opening edge file '" + filename + "'.");
        }

        std::filesystem::path path{filename};
        std::cout << "\tReading '" << path.string() << "' (" << std::filesystem::file_size(path) << " byte)." << std::endl;
        long long edge_count = 0;
        long long lines_skipped = 0;

        std::string line;


        // Skip first line, this defines the structure of the file.
        std::getline(file, line);
        if (line.ends_with('\r')) {
            line.erase(line.length() - 1, 1);
        }
        std::vector<std::string> columns;
        const size_t expected_nbr_of_columns = split_on_char(line, '\t', columns);

        // Check if the provided structure is compatible with the provided indices.
        if (this->idx_start_node_id >= columns.size()) {
            throw std::runtime_error("This file does not define enough columns to read the start-node-id at index "
                + std::to_string(this->idx_start_node_id)
                + ". Expected at least " + std::to_string(this->idx_start_node_id+1) + " columns, got " + std::to_string(columns.size()) + ".");
        }
        if (this->idx_end_node_id >= columns.size()) {
            throw std::runtime_error("This file does not define enough columns to read the end-node-id at index "
                + std::to_string(this->idx_end_node_id)
                + ". Expected at least " + std::to_string(this->idx_end_node_id+1) + " columns, got " + std::to_string(columns.size()) + ".");
        }
        const size_t highest_idx = *std::max_element(this->idx_edge_type.begin(), this->idx_edge_type.end());
        if (highest_idx >= columns.size()) {
            throw std::runtime_error("This file does not define enough columns to read part of the edge-type at index "
                + std::to_string(highest_idx)
                + ". Expected at least " + std::to_string(highest_idx+1) + " columns, got " + std::to_string(columns.size()) + ".");
        }

        // Confirm the indices to the user.
        std::cout << "\t\tReading the unique start-node-id from column '" << columns[this->idx_start_node_id] << "'." << std::endl;
        std::cout << "\t\tReading the unique end-node-id from column '" << columns[this->idx_end_node_id] << "'." << std::endl;
        std::cout << "\t\tReading the edge-type as a composite from columns:";
        for (auto idx: this->idx_edge_type) {
            std::cout << " '" << columns[idx] << "'";
        }
        std::cout << "." << std::endl;



        while (std::getline(file, line)) {
            // Remove stray \r characters. These may appear in files created under windows (\r\n instead of just \n)
            if (line.ends_with('\r')) {
                line.erase(line.length() - 1, 1);
            }

            // Split the line on the provided delimiter.
            size_t actual_nbr_of_columns = split_on_char(line, '\t', columns);
            if (actual_nbr_of_columns != expected_nbr_of_columns) {
                ++lines_skipped;
                if (debug) {
                    std::cout << "\t\tSkipping invalid line: '" << line << "'" << std::endl;
                }
                continue;
            }

            // Read the node-IDs and edge-type based on the configuration. The edge-type can be a composite from multiple columns.
            std::string start_node_id = columns[this->idx_start_node_id];
            std::string end_node_id = columns[this->idx_end_node_id];
            std::string edge_type = "";
            for (auto idx: this->idx_edge_type) {
                edge_type.append(columns[idx] + "_");
            }
            if (edge_type.size() != 0) {edge_type.erase(edge_type.length() - 1, 1);} // Remove the trailing underscore.

            model.readEdge(start_node_id, end_node_id, edge_type);
            ++edge_count;
        }
        file.close();
        std::cout << "\t\tRead: " << edge_count << " Edges. Skipped " << lines_skipped << " lines." << std::endl;
    }

    return model.process(meta_data, seed);
}

