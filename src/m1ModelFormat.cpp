#include "../src/graphgenerator_types.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <filesystem>

constexpr uint8_t RESERVED_BYTES_FOR_STRING_CONSTRUCTION = 128;

struct Meta_Record {
    std::string name;
    std::map<std::string, std::string> values;
};

// We allow for continuous ranges of NodeIDs. This simplifies actually scaling the graph significantly.
// "Actual" NodeIDs are later recovered by floor(x)+1 for the start of an interval and by floor(x) for the interval.
struct Node_Record {
    ContinuousNodeID startID;
    ContinuousNodeID endID;
    std::string node_type;
    bool operator< (const Node_Record& rhs) const {
        return (this->startID < rhs.startID) || (this->startID == rhs.startID && this->endID < rhs.endID);
    }
};

// We allow for continuous ranges of NodeIDs. This simplifies actually scaling the graph significantly.
// "Actual" NodeIDs are later recovered by floor(x)+1 for the start of an interval and by floor(x) for the interval.
struct Edge_Block {
    ContinuousNodeID startX;
    ContinuousNodeID endX;
    ContinuousNodeID startY;
    ContinuousNodeID endY;
    Probability expression_probability;
    bool operator< (const Edge_Block& rhs) const {
        return (this->startX < rhs.startX) || (this->startX == rhs.startX && this->startY < rhs.startY);
    }
};

struct Edge_Record {
    std::string edge_type;
    std::vector<Edge_Block> blocks;
    bool operator< (const Edge_Record& rhs) const {
        return (this->edge_type < rhs.edge_type);
    }
};

struct m1_data {
    Meta_Record meta;
    std::vector<Node_Record> nodes;
    std::vector<Edge_Record> edges;
};

enum ReaderMode {
    NONE,
    META,
    NODES,
    EDGES,
};

// De-Serializes a given file of m1-format into a struct of m1_data.
// Some recoverable deviations from the definition of the m1-format are tolerated, but warned about.
m1_data read_m1_file(const std::string& file_name) {
    m1_data result = {};

    std::ifstream file(file_name.data());
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file " + file_name + ".");
    }

    bool has_meta = false, has_node = false, has_edges = false;
    std::string current_edge_type;
    std::vector<Edge_Block> current_blocks;
    ReaderMode mode = NONE;
    std::string line;
    while (std::getline(file, line)) {
        // Remove stray \r characters. These may appear in files created under windows (\r\n instead of just \n)
        if (line.ends_with('\r')) {
            line.erase(line.length() - 1, 1);
        }
        // Empty lines are disregarded.
        if (line.empty()) {continue;}

        // Lines starting with '#' indicate the declaration of a new block. Change mode accordingly.
        if (line.starts_with('#')) {
            if (line.starts_with("# META")) {mode = META;}
            else if (line.starts_with("# NODES")) {mode = NODES;}
            else if (line.starts_with("# EDGES")) {
                // A block of type EDGES requires further initialization.
                mode = EDGES;
                // If any edge-blocks have been read within the current edge type, save them to the data structure.
                if (!current_blocks.empty()) {
                    result.edges.push_back(Edge_Record(current_edge_type, current_blocks));
                    has_edges = true;
                }
                size_t idx = line.find('=');
                // Reset the content of the current vector of edge-blocks.
                current_edge_type = line.substr(idx + 1);
                current_blocks = std::vector<Edge_Block>();
            }
            else {
                std::string message;
                            message.append("Encountered unexpected directive '");
                            message.append(line);
                            message.append("' while parsing m1-file (");
                            message.append(file_name);
                            message.append("). The file may be malformed.");
                throw std::runtime_error(message);
            }
            continue;
        }

        switch (mode) {
            case NONE: {
                std::string message;
                message.append("Encountered unexpected line '");
                message.append(line);
                message.append("' in mode NONE while parsing m1-file (");
                message.append(file_name);
                message.append(").");
                throw std::runtime_error(message);
            }

            case META: {
                std::stringstream ss_meta(line);
                std::string key;
                std::string value;
                std::getline(ss_meta, key, '=');
                std::getline(ss_meta, value);
                // Check for incomplete data in the line.
                if (key.empty() || value.empty()) {
                    std::cerr << "\tEncountered incomplete line (" << line << ") in mode META while parsing m1-file ("
                                        << file_name << "). Skipping." << std::endl;
                    break;
                }
                // We explicitly define a name in the description of the m1-standard. This is accounted for in a designated
                //      variable, other keys are thrown into a map to be used at the informed users' discretion.
                if (key == "NAME") {
                    result.meta.name = value;
                    has_meta = true;
                }  else {
                    result.meta.values[key] = value;
                }
                break;
            }

            case NODES: {
                std::stringstream ss_nodes(line);
                std::string start;
                std::string start_dec;
                std::string end;
                std::string end_dec;
                std::string node_type;
                std::getline(ss_nodes, start, ',');
                std::getline(ss_nodes, end, ',');
                std::getline(ss_nodes, node_type);
                // Check for incomplete data in the line.
                if (start.empty() || end.empty() || node_type.empty()) {
                    std::cerr << "\tEncountered incomplete line (" << line << ") in mode NODES while parsing m1-file ("
                                        << file_name << "). Skipping." << std::endl;
                    break;
                }
                // If the line is complete, try to parse it.
                ContinuousNodeID startID = 0;
                ContinuousNodeID endID = 0;
                try {
                    startID = std::stold(start);
                    endID = std::stold(end);
                } catch (std::exception& e) {
                    std::cerr << "\tCould not parse '" << start << "' or '" << end << "' into a valid uLL in line ("
                                << line << ") in mode NODES while parsing m1-file (" << file_name << "). Skipping."
                                << std::endl;
                    std::cerr << e.what() << std::endl;
                    break;
                }

                // Finally create a new node-record for valid lines.
                result.nodes.emplace_back(Node_Record{startID, endID, node_type});
                has_node = true;
                break;
            }

            case EDGES: {
                std::stringstream ss_edges(line);
                std::string startX;
                std::string endX;
                std::string startY;
                std::string endY;
                std::string probability;
                std::getline(ss_edges, startX, ',');
                std::getline(ss_edges, endX, ',');
                std::getline(ss_edges, startY, ',');
                std::getline(ss_edges, endY, ',');
                std::getline(ss_edges, probability);
                // Check for incomplete data in the line.
                if (startX.empty() || endX.empty() || startY.empty() || endY.empty() || probability.empty()) {
                    std::cerr << "\tEncountered incomplete line (" << line << ") in mode EDGES while parsing m1-file ("
                                        << file_name << "). Skipping." << std::endl;
                    break;
                }
                // If the line is complete, try to parse it.
                ContinuousNodeID startXID;
                ContinuousNodeID endXID;
                ContinuousNodeID startYID;
                ContinuousNodeID endYID;
                Probability f_probability;
                try {
                    startXID = std::stold(startX);
                    endXID = std::stold(endX);
                    startYID = std::stold(startY);
                    endYID = std::stold(endY);
                } catch (std::exception& e) {
                    std::cerr << "\tCould not parse one or more elements into a valid uLL in line ("
                                << line << ") in mode NODES while parsing m1-file (" << file_name << "). Skipping."
                                << std::endl;
                    std::cerr << e.what() << std::endl;
                    break;
                }

                try {
                    f_probability = std::stof(probability);
                } catch (std::exception& e) {
                    std::cerr << "\tCould not parse '" << probability << "' into a valid float in line ("
                                << line << ") in mode NODES while parsing m1-file (" << file_name << "). Skipping."
                                << std::endl;
                    std::cerr << e.what() << std::endl;
                    break;
                }

                // Finally create a new block for valid lines.
                // Full records for this edge-type are only added once another edge-type is detected or parsing ends.
                current_blocks.emplace_back(Edge_Block(startXID, endXID, startYID, endYID, f_probability));
                break;
            }
        }
    }

    // If any edge-blocks have been read within the current edge type, save them to the data structure.
    if (!current_blocks.empty()) {
        result.edges.push_back(Edge_Record(current_edge_type, current_blocks));
        has_edges = true;
    }

    if (!has_meta) {throw std::runtime_error("'" + file_name + "' is missing a valid META-Section with at least a 'NAME=...' declaration.");}
    if (!has_node) {throw std::runtime_error("'" + file_name + "' is missing a valid NODES-Section with at least one node type.");}
    if (!has_edges) {throw std::runtime_error("'" + file_name + "' is missing a valid EDGES-Section with at least an edge type.");}

    std::cout << "\tRead " << result.nodes.size() << " type(s) of nodes and " << result.edges.size() << " type(s) of edges." << std::endl;
    return result;
}

// Serializes a passed struct of m1_data to a conformant m1-model-file. Certain deviations from the definition of the m1-format
//      are tolerated (i.e. not passing a model-name), but receive a warning on std::cerr.
// Returns the (approximate) number of bytes written.
size_t write_m1_file(const std::string& file_name, const m1_data& data) {
    std::filesystem::path file_path(file_name);
    if (file_path.has_parent_path() && !exists(file_path.parent_path())) {
        throw std::runtime_error("Directory does not exist: " + file_path.parent_path().string());
    }

    std::ofstream out_file(file_name);
    if (!out_file) {
        throw std::runtime_error("Directory does not exist: " + file_path.string());
    }
    size_t bytes_at_start = out_file.tellp();

    // Write the provided meta-data.
    if (data.meta.name.empty()) {std::cerr << "\tWarning: The given model must provide a name." << std::endl;}
    out_file << "# META\n";
    out_file << "NAME=" << data.meta.name << std::endl;
    for (const auto& [key, value] : data.meta.values) {
        if (key.find('=') != std::string::npos) {
            throw std::runtime_error("Equal-Signs '=' are not allowed as part of the key given in {" + key + ": " + value + "}");
        }
        if (key.find('\n') != std::string::npos or value.find('\n') != std::string::npos) {
            throw std::runtime_error("Newline-Characters are not allowed as part of the Key/Value-Pair given in : {" + key + ":" + value + "}");
        }
        out_file << key << "=" << value << std::endl;
    }
    out_file << std::endl;

    // Write the provided node-data.
    out_file << "# NODES\n";
    std::string node_str;
    node_str.reserve(RESERVED_BYTES_FOR_STRING_CONSTRUCTION);
    // #pragma omp parallel for private(node_str)
    for (const auto& [startID, endID, node_type]: data.nodes) {
        if (node_type.find('\n') != std::string::npos) {
            throw std::runtime_error("Newline-Characters are not allowed as part of the node-type given: " + node_type);
        }
        // Manual string-construction for performance / multithreading reasons.
        node_str = "";
        node_str.append(std::to_string(startID));
        node_str.push_back(',');
        node_str.append(std::to_string(endID));
        node_str.push_back(',');
        node_str.append(node_type);
        node_str.push_back('\n');
        out_file << node_str;
    }
    out_file << std::endl;

    // Write the provided edge-data.
    for (const auto& edge_type: data.edges) {
        if (edge_type.edge_type.find('\n') != std::string::npos) {
            throw std::runtime_error("Newline-Characters are not allowed as part of the edge-type given: " + edge_type.edge_type);
        }
        out_file << "# EDGES=" << edge_type.edge_type << std::endl;
        std::string edge_str;
        edge_str.reserve(RESERVED_BYTES_FOR_STRING_CONSTRUCTION);
        // #pragma omp parallel for private(edge_str)
        for (auto &[startX, endX, startY, endY, expression_probability] : edge_type.blocks) {
            // if (expression_probability > 1.0f || expression_probability < 0.0f) {throw std::runtime_error("All expression-probabilities must be in the Interval [0,1].");}
            edge_str = "";
            edge_str.append(std::to_string(startX));
            edge_str.push_back(',');
            edge_str.append(std::to_string(endX));
            edge_str.push_back(',');
            edge_str.append(std::to_string(startY));
            edge_str.push_back(',');
            edge_str.append(std::to_string(endY));
            edge_str.push_back(',');
            edge_str.append(std::to_string(expression_probability));
            edge_str.push_back('\n');
            out_file << edge_str;
        }
        out_file << std::endl;
    }

    return static_cast<size_t>(out_file.tellp()) - bytes_at_start;
}

// Scale the size of a given graph described by the m1_data-struct with a non-zero scaling factor.
// A completely new struct is constructed (~deep copy) without side effects on the passed struct.
m1_data scale_m1_data(m1_data& data, const float scale) {
    if (scale == 0.0f) {throw std::runtime_error("Scale must be greater than zero.");}
    if (scale < 1.0f) {std::cerr << "\tWarning: Downscaling a dataset can have a serious impact on the resulting graphs! Proceed with caution." << std::endl;}

    m1_data result = {};
    result.meta.name = data.meta.name;
    for (const auto& [k, v]: data.meta.values) {
        result.meta.values[k] = v;
    }

    // Set a new key in the META-Block to indicate the new scale of the model, relative to the original graph.
    double old_scale = 1.0f;
    if (data.meta.values.contains("SCALE")) {
        try {
            old_scale = std::stof(data.meta.values["SCALE"]);
        } catch (const std::exception& e) {
            std::cerr << "\tWarning: Encountered non-float value when parsing the META-Key 'SCALE'."
                      <<  "The new value of SCALE may not be accurate." << std::endl;
            std::cerr << e.what() << std::endl;
        }
        if (old_scale <= 0.0f) {
            std::cerr << "\tWarning: Encountered a negative value when parsing the META-Key 'SCALE'."
                      <<  "The new value of SCALE may not be accurate." << std::endl;
        }
    }
    result.meta.values["SCALE"] = std::to_string(old_scale * scale);

    // As we allow continuous ranges of nodes in the m1-format, scaling is as simple as multiplying the
    //      both elements of the range by the scaling factor.
    result.nodes.reserve(result.nodes.size());
    for (const auto& [startID, endID, node_type]: data.nodes) {
        result.nodes.emplace_back(Node_Record(startID*scale, endID*scale, node_type));
    }

    result.edges.reserve(data.edges.size());
    Amount new_model_failures = 0;
    Amount total_blocks = 0;
    for (const auto& [edge_type, blocks]: data.edges) {
        Edge_Record r = {};
        r.edge_type = edge_type;
        r.blocks.reserve(blocks.size());

        for (auto [startX, endX, startY, endY, expression_probability]: blocks) {
            // As the number of nodes is increased, the expression-probability needs to be reduced by the same factor.
            //      This retains the expected In-/Out-Degrees of the nodes.
            Probability adapted_probability = expression_probability / scale;
            // Clamp the probability to a maximum of 1.0 and warn the user. This should only happen when scaling down graphs.
            if (adapted_probability > 1.0f) {
                adapted_probability = 1.0f;
                ++new_model_failures;
            }
            r.blocks.emplace_back(Edge_Block(startX*scale, endX*scale, startY*scale, endY*scale, adapted_probability));
            ++total_blocks;
        }
        result.edges.emplace_back(r);
    }

    std::cout << "\tNew scale: x" << result.meta.values["SCALE"] << " of original." << std::endl;

    if (new_model_failures > 0) {
        std::cerr << '\t' << new_model_failures << " (" << new_model_failures / (total_blocks / static_cast<long double>(100))
            << "%) model-failures (block-probability > 1.0) remaining after scaling." << std::endl;
    }

    return result;
}

