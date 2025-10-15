#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <tuple>
#include <cstring>
#include <thread>


using Record = std::tuple<NodeID, NodeID, NodeID, NodeID, Probability>;

constexpr std::int8_t MAX_ALLOWED_TYPE_LENGTH = 64;
constexpr std::int8_t MAX_NUM_DIGITS = 20;  // The highest number of digits for a 64bit uint in Base10.
constexpr std::int8_t MAX_OUT_STRING_LEN = MAX_ALLOWED_TYPE_LENGTH + 2*(MAX_NUM_DIGITS+1);    // Change according to output format.

constexpr std::int64_t MAX_BUFFER_SIZE = 1e5;
constexpr std::int64_t MAX_BUFFER_SAFETY_MARGIN = 500;


// Custom Int-to-(ASCII)-String function. Allows only unsigned 64-bit integers in base10.
//     Safety-Checks removed, use at your own peril!
// Adapted and optimized from: https://stackoverflow.com/a/12386915
int unsafe_u64Int_to_str(char* sp, uint64_t value) {    // TODO: Tests needed..
    char buffer[20]; // 20 Digits are sufficient for all 64bit unsigned integers.
    char *tp = buffer;

    do {
        *tp++ = (value % 10)+'0';   // Radix is fixed at 10.
        value /= 10;
    } while (value);    // Do-While to correctly handle value=0

    const int len = tp - buffer;

    while (tp > buffer) {
        *sp++ = *--tp;
    }
    *sp = '\0';

    return len;
}

inline NodeID convert_start_of_block(const long double x) {
    return static_cast<NodeID>(x) + 1;
}
inline NodeID convert_end_of_block(const long double x) {
    return static_cast<NodeID>(x);
}

// Convert a given Edge-Record into the proper input-format.
//  This recovers the integer-valued NodeIDs for the start/end of a block from the real-valued representation used
//  in the model. Reduce given probabilities to the interval [0,1].
std::vector<Record> read_edge_block_data(const Edge_Record& data) {
    if (data.edge_type.size() > MAX_ALLOWED_TYPE_LENGTH) {
        throw std::runtime_error("The edge-type '" + data.edge_type +"' is larger than the allowed size of "
            + std::to_string(MAX_ALLOWED_TYPE_LENGTH) + " chars. Consider increasing MAX_ALLOWED_TYPE_LENGTH if necessary.");
    }
    std::vector<Record> res = {};
    res.reserve(data.blocks.size());
    for (auto &[startX, endX, startY, endY, expression_probability]: data.blocks) {
        // Restrict probabilities to the interval [0,1]. This is done here to allow for more accurate scaling of the model.
        Probability prob = expression_probability;
        const size_t s_X = convert_start_of_block(startX);
        const size_t e_X = convert_end_of_block(endX);
        const size_t s_Y = convert_start_of_block(startY);
        const size_t e_Y = convert_end_of_block(endY);

        if (e_X < s_X || e_Y < s_Y) {continue;} // Can occur during downsizing due to strange rounding. TODO: Look into root cause!
        if (prob > 1) {prob = 1;}

        res.emplace_back(std::make_tuple(s_X, e_X, s_Y, e_Y, prob));
    }
    return res;
}


void multithread_generate_graph(const std::vector<Record>& data, const size_t workload_start, const size_t workload_end, std::ofstream& output,
    const std::mt19937_64::result_type seed, const std::string& e_type, std::mutex& w_lock) {

    char buffer[MAX_BUFFER_SIZE] = "";
    char* buffer_pos = &buffer[0];

    std::mt19937_64 rdm_gen(seed);
    std::uniform_real_distribution<float> uniform_f_distr(std::nextafter(0.0f, 1.0f), std::nextafter(1.0f, 0.0f));

    for (size_t idx = workload_start; idx <= workload_end; ++idx) {
        const auto& [startX, endX, startY, endY, prob] = data[idx];
        // Improved drawing from the geometric distribution using the method from Luc Devroye.
        //     L. Devroye "Non-Uniform Random Variate Generation", Springer Verlag (1986), p.499 ff
        // As the denominator ln(1-p) is constant for given p, we precompute 1 / ln(1-p) for the block.
        // We later calculate log2, which we then need to multiply with ln(2) = 0.69314718 to approximate the nat. logarithm.
        const float devroye_denominator = (1 / std::log(1-prob)) * 0.69314718f;

        // Pick edges within the block.
        const Amount len_x = (endX - startX) + 1;
        Amount offset_x = 0;
        NodeID idx_y = startY;
        while (true) {
            const Amount jump_distance = 1 + static_cast<int>(std::ceil(std::log2(uniform_f_distr(rdm_gen)) * devroye_denominator));
            const Amount next_offset = offset_x + jump_distance;

            offset_x = next_offset % len_x;
            idx_y += next_offset / len_x;

            if (idx_y > endY) [[unlikely]]
                {break;}

            // Use a customized conversion-function to write the Node-ID's to the output-buffer.
            buffer_pos += unsafe_u64Int_to_str(buffer_pos, startX+offset_x);
            *buffer_pos++ = '\t';
            buffer_pos += unsafe_u64Int_to_str(buffer_pos, idx_y);
            *buffer_pos++ = '\t';
            strcpy(buffer_pos, e_type.c_str());
            buffer_pos += e_type.size();
            *buffer_pos++ = '\n';

            // When the buffer is close to being full, write it to the output buffer and reset it.
            if (buffer_pos >= &buffer[MAX_BUFFER_SIZE-MAX_BUFFER_SAFETY_MARGIN-1]) [[unlikely]] {
                w_lock.lock();
                output.write(buffer, buffer_pos - buffer);
                w_lock.unlock();

                buffer_pos = &buffer[0];
                buffer[0] = '\0';
            }
        }
    }

    // When the workload is completed, write the remaining data in the buffer to the file.
    if (buffer_pos > &buffer[0]) {
        w_lock.lock();
        output.write(buffer, buffer_pos - buffer);
        w_lock.unlock();
    }
}

void generate_graph(const std::string& node_file_name, const std::string& edge_file_name,
    const m1_data& data, const std::mt19937_64::result_type seed) {
    // Try to open the output files. We keep the size of the files after opening to calculate the amount of data written later.
    std::ofstream node_file;
    node_file.open(node_file_name);
    if (!node_file.is_open()) {
        throw std::runtime_error("Could not open output file: " + node_file_name);
    }
    const size_t node_bytes_at_start = node_file.tellp();

    std::ofstream edge_file;
    edge_file.open(edge_file_name, std::ios::trunc | std::ios::binary);
    if (!edge_file.is_open()) {
        throw std::runtime_error("Could not open output file: " + edge_file_name);
    }
    const size_t edge_bytes_at_start = edge_file.tellp();


    // Write the node-file: The ID's of all blocks are filled out.
    for (auto &[startID, endID, node_type] : data.nodes) {
        const Node_Type n_type = node_type;
        NodeID start = convert_start_of_block(startID);
        NodeID end = convert_end_of_block(endID);

        std::string opt_string;
        opt_string.reserve(MAX_OUT_STRING_LEN);
        // #pragma omp parallel for private(opt_string)
        for (NodeID i = start; i <= end; ++i) {
            opt_string = "";
            opt_string.append(std::to_string(i));
            opt_string.push_back('\t');
            opt_string.append(n_type);
            opt_string.push_back('\n');
            node_file << opt_string;
        }
    }
    std::cout << "\t\tWrote " << static_cast<size_t>(node_file.tellp()) - node_bytes_at_start << " bytes into the provided node-file." << std:: endl;
    node_file.close();

    // Convert Edge-Block-Data from the model into the preferred form for construction.
    std::vector<std::pair<Edge_Type, std::vector<Record>>> block_data = {};
    block_data.reserve(data.edges.size());

    for (const auto &e: data.edges) {
        block_data.emplace_back(std::make_pair(e.edge_type, read_edge_block_data(e)));
    }


    // TODO: Replace with xorshift or xoshiro (https://prng.di.unimi.it/xoshiro256plus.c)
    // TODO: Optionally use ankerl::nanobench::Rng (Uses the somewhat dodgy RomuDuoJr-Algorithm.)
    // TODO: Verify accurate seeding (including within threads!)
    std::mt19937_64 rdm_gen(seed);
    const auto start = std::chrono::high_resolution_clock::now();

    for (const auto& [e_type, block] : block_data) {

        size_t n_threads = std::thread::hardware_concurrency() - 1;
        if (n_threads <= 1) {n_threads = 1;}

        size_t workload_size = block.size() / n_threads;
        size_t overflow_workload_size = workload_size % n_threads;

        std::vector<std::thread> threads;
        std::mutex write_lock;

        // Don't bother with the threading-overhead for small work sizes.
        if (block.size() < 100) {
            multithread_generate_graph(block, 0, block.size()-1, edge_file, rdm_gen(), e_type, write_lock);
            continue;
        }

        // Distribute the blocks over all available threads.
        size_t idx_start = 0;
        size_t idx_end = overflow_workload_size + workload_size - 1;
        for (size_t thread_no = 0; thread_no < n_threads; ++thread_no) {

            threads.emplace_back(
                std::thread(multithread_generate_graph,
                    std::cref(block), idx_start, idx_end, std::ref(edge_file), rdm_gen(),
                    std::cref(e_type), std::ref(write_lock)
                ));
            idx_start = idx_end+1;
            idx_end = idx_start + workload_size;
            if (idx_end > block.size()) {idx_end = block.size()-1;}
        }

        // Wait for all threads to complete before advancing to the next edgetype
        for (auto& thread: threads) {thread.join();}
    }

    size_t bytes_written = static_cast<size_t>(edge_file.tellp()) - edge_bytes_at_start;
    edge_file.close();

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\t\tWrote " << bytes_written / 1.0e9L << " GB into the provided edge-file in " << duration.count() / 1000.0L << " seconds. \n";
    std::cout << "\t\tGenerated with a rate of " << (bytes_written / 1.0e9L) / (duration.count() / 1000.0L) << " GB/s. \n";
}
