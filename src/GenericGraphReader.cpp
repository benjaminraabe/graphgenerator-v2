#include <unordered_map>
#include <random>
#include <string>
#include <unordered_set>
#include "../src/graphgenerator_types.h"


struct Edge_Type_Container {
    Amount sum_of_in_degrees = 0;
    Amount sum_of_out_degrees = 0;
    Amount number_of_nodes_with_in_degree = 0;
    Amount number_of_nodes_with_out_degree = 0;

    std::vector<std::pair<Degree, Amount>> in_degrees;
    std::vector<std::pair<Degree, Amount>> out_degrees;
};

struct Node_Type_Container {
    Amount node_count = 0;
    Node_Type node_type;
    std::unordered_map<Edge_Type, Edge_Type_Container> edge_data;
    [[nodiscard]] bool has_edge_type(const Edge_Type &t) const {return edge_data.contains(t);}
};


class GenericGraphReader {
public:
    GenericGraphReader();

    void readNode(const std::string& node, const Node_Type &node_type);
    void readEdge(const std::string& start, const std::string& end, const Edge_Type &edge_type);

    m1_data process(std::map<std::string, std::string> meta_data, std::mt19937_64::result_type seed);

    Amount node_count;
    std::unordered_map<Edge_Type, Amount> edge_count;

    // Number of Type-Type-Transitions for each Edge-Type
    std::unordered_map<Edge_Type, std::map<std::pair<Node_Type, Node_Type>, Amount> > sbm_matrix;

    // Count the number of occurrences for every Type/Color to calculate a distribution in the end.
    std::unordered_map<Node_Type, Amount> node_types;
    std::unordered_set<Edge_Type> edge_colors;

private:
    // Save the nodeType for every read node. Needed later to map the edges to the correct node-type
    std::unordered_map<std::string, Node_Type> nodes_to_types;

    // Count Incoming/Outgoing Edges for every Node
    std::unordered_map<Edge_Type, std::map<std::string, Degree> > in_degrees;
    std::unordered_map<Edge_Type, std::map<std::string, Degree> > out_degrees;
};

GenericGraphReader::GenericGraphReader(): node_count(0) {}

void GenericGraphReader::readNode(const std::string& node, const Node_Type &node_type) {
    ++this->node_count;

    // Increase the count of the node-color
    ++this->node_types[node_type];

    // Remember this node for future lookups
    this->nodes_to_types[node] = node_type;
}


void GenericGraphReader::readEdge(const std::string& start, const std::string& end, const Edge_Type &edge_type) {
    ++this->edge_count[edge_type];

    // Increase the entry in the SBM-Matrix
    Node_Type type_start = this->nodes_to_types[start];
    Node_Type type_end = this->nodes_to_types[end];
    ++this->sbm_matrix[edge_type][std::make_pair(type_start, type_end)];

    // Increase In/Out Degree of the node
    ++this->out_degrees[edge_type][start];
    ++this->in_degrees[edge_type][end];

    // Add the Color to the set of Edge-Colors
    this->edge_colors.insert(edge_type);
}

m1_data GenericGraphReader::process(std::map<std::string, std::string> meta_data, std::mt19937_64::result_type seed) {
    std::mt19937 random_source(seed);

    std::cout << "\tCreating model...";

    // Setup all Node/Edge-Containers
    std::unordered_map<Node_Type, Node_Type_Container> nt_containers;
    for (auto [n_type, cnt]: this->node_types) {
        Node_Type_Container container = {};
        container.node_type = n_type;
        container.node_count = cnt;

        for (auto e_type: this->edge_colors) {
            container.edge_data[e_type] = Edge_Type_Container();
        }
        nt_containers[n_type] = container;
    }

    // Construct the degree-distribution for every edge-type and node-type
    std::unordered_map<Node_Type, std::unordered_map<Edge_Type, std::unordered_map<Degree, Amount>>> in_distribution;
    std::unordered_map<Node_Type, std::unordered_map<Edge_Type, std::unordered_map<Degree, Amount>>> out_distribution;
    for (const auto &[e_type, nodes]: this->in_degrees) {
        for (const auto &[node, deg]: nodes) {
            in_distribution[this->nodes_to_types[node]][e_type][deg]++;
        }
    }
    for (const auto &[e_type, nodes]: this->out_degrees) {
        for (const auto &[node, deg]: nodes) {
            out_distribution[this->nodes_to_types[node]][e_type][deg]++;
        }
    }

    // Read the degree-distributions into the proper container.
    for (const auto &[n_type, e_cont]: in_distribution) {
        for (const auto &[e_type, nodes]: e_cont) {
            for (const auto &[deg, amount]: nodes) {
                nt_containers[n_type].edge_data[e_type].in_degrees.emplace_back(std::make_pair(deg, amount));
                nt_containers[n_type].edge_data[e_type].number_of_nodes_with_in_degree += amount;
                nt_containers[n_type].edge_data[e_type].sum_of_in_degrees += deg*amount;
            }
        }
    }
    for (const auto &[n_type, e_cont]: out_distribution) {
        for (const auto &[e_type, nodes]: e_cont) {
            for (const auto &[deg, amount]: nodes) {
                nt_containers[n_type].edge_data[e_type].out_degrees.emplace_back(std::make_pair(deg, amount));
                nt_containers[n_type].edge_data[e_type].number_of_nodes_with_out_degree += amount;
                nt_containers[n_type].edge_data[e_type].sum_of_out_degrees += deg*amount;
            }
        }
    }

    // Pad with 0-degree nodes where necessary.
    for (auto &[n_type, n_container]: nt_containers) {
        for (auto &[e_type, e_container]: n_container.edge_data) {
            if (e_container.number_of_nodes_with_in_degree < n_container.node_count) {
                e_container.in_degrees.emplace_back(std::make_pair(0, n_container.node_count - e_container.number_of_nodes_with_in_degree));
            }
            if (e_container.number_of_nodes_with_out_degree < n_container.node_count) {
                e_container.out_degrees.emplace_back(std::make_pair(0, n_container.node_count - e_container.number_of_nodes_with_out_degree));
            }

            // Randomly shuffle the degree-ranges to attain the semblance of statistical independence...
            //  Sort the container before shuffling to make the results reproducible
            // TODO: The effects of this need to be investigated. In the worst case we can have signification correlation
            //      between degrees of nodes, as we cannot shuffle the degree-assignment fully, without breaking up
            //      the continuous blocks of probabilities.
            std::sort(e_container.in_degrees.begin(), e_container.in_degrees.end());
            std::shuffle(e_container.in_degrees.begin(), e_container.in_degrees.end(), random_source);
            std::sort(e_container.out_degrees.begin(), e_container.out_degrees.end());
            std::shuffle(e_container.out_degrees.begin(), e_container.out_degrees.end(), random_source);
        }
    }

    std::vector<Node_Type_Container> work_container;
    // Construct the actual data-structure.
    for (auto &[n_type, nt_container]: nt_containers) {
        work_container.emplace_back(nt_container);
    }


    m1_data result_data = {};
    result_data.meta.values["SCALE"] = "1.0";
    result_data.meta.name = "Unnamed graph model";
    for (const auto &[k, v]: meta_data) {
        if (k == "NAME") {result_data.meta.name = v; continue;}
        result_data.meta.values[k] = v;
    }



    // Parse Nodes
    Amount current_id = 0;
    for (const auto &container: work_container) {
        result_data.nodes.emplace_back(Node_Record(current_id, (current_id+container.node_count), container.node_type));
        current_id += container.node_count;
    }

    // Parse Edge-Blocks for every Edge-Type
    Amount failed_ddcsbm_probabilities = 0;
    Amount total_blocks = 0;
    for (const auto &e_type: this->edge_colors) {
        Edge_Record record = {};
        record.edge_type = e_type;

        Amount outer_id_x = 0;

        for (auto container_x: work_container) {
            // If the block does not have any nodes with edges with this edge type, skip. (=> Expression probability would be 0 anyway)
            if (!container_x.has_edge_type(e_type) || container_x.edge_data[e_type].number_of_nodes_with_out_degree == 0) {
                outer_id_x += container_x.node_count;
                continue;
            }

            Amount outer_id_y = 0;
            // If the block does not have any nodes with edges with this edge type, skip. (=> Expression probability would be 0 anyway)
            for (auto container_y: work_container) {

                if (!container_y.has_edge_type(e_type) || container_y.edge_data[e_type].number_of_nodes_with_in_degree == 0) {
                    outer_id_y += container_y.node_count;
                    continue;
                }

                Amount edges_between_types = this->sbm_matrix[e_type][std::make_pair(container_x.node_type, container_y.node_type)];
                Amount current_id_x = outer_id_x;
                for (const auto &[deg_x, amount_x]: container_x.edge_data[e_type].out_degrees) {
                    Amount current_id_y = outer_id_y;
                    for (const auto &[deg_y, amount_y]: container_y.edge_data[e_type].in_degrees) {
                        const Amount sum_of_out = container_x.edge_data[e_type].sum_of_out_degrees;
                        const Amount sum_of_in = container_y.edge_data[e_type].sum_of_in_degrees;
                        Probability prob = 0.0;

                        // DDcSBM-Formula. Preempt potential zero-division-errors.
                        if ( sum_of_out > 0 && sum_of_in > 0) {
                            prob = static_cast<float>(edges_between_types) * (static_cast<float>(deg_x) / sum_of_out) * (static_cast<float>(deg_y) / sum_of_in);
                        }

                        // Normalize to the Interval [0,1]. Failures in the model are recorded for statistics.
                        if (prob > 1) {
                            // prob = 1;
                            ++failed_ddcsbm_probabilities;
                        }

                        // Fallback for 0-Probabilities: Only add blocks if their expression-probability is positive.
                        if (prob > 0) {
                            record.blocks.emplace_back(Edge_Block(current_id_x, current_id_x+amount_x,
                                current_id_y, current_id_y+amount_y, prob));
                            ++total_blocks;
                        }


                        current_id_y += amount_y;
                    }
                    current_id_x += amount_x;
                }
                outer_id_y += container_y.node_count;
            }
            outer_id_x += container_x.node_count;
        }
        std::sort(record.blocks.begin(), record.blocks.end());
        result_data.edges.emplace_back(record);
    }

    // As we used unordered maps before, the order of the container is non-deterministic.
    //  We sort the containers once before processing to allow for reproducible generation.
    std::sort(result_data.edges.begin(), result_data.edges.end());
    std::sort(result_data.nodes.begin(), result_data.nodes.end());

    std::cout << " Done." << std::endl;
    if (failed_ddcsbm_probabilities > 0) {
        std::cout << "\tModel failure (p > 1.0) on " << failed_ddcsbm_probabilities << " out of " << total_blocks << " blocks. ("
        << failed_ddcsbm_probabilities / (total_blocks / static_cast<long double>(100)) << "%)" << std::endl;
    }

    return result_data;
}