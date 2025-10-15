#include <string>
#include <iostream>

#include "src/m1ModelFormat.cpp"
#include "src/GenericGraphReader.cpp"
#include "src/TSVReader.cpp"
#include "src/Generator.cpp"
#include "src/s1ScriptFormat.cpp"


int main(int argc, char * argv[]) {

    // Complain if no instructions have been passed!
    if (argc <= 1) {
        std::cout << "No instructions passed to the generator. Call the program with '-help' for documentation." << std::endl;
        return -1;
    }

    // Parse the starting-instructions received from the command-line.
    std::vector<std::string> args_raw;
    for (int i = 1; i < argc; i++) {
        args_raw.push_back(argv[i]);
    }
    std::string starting_script = args_raw[0];
    for (size_t i = 1; i < args_raw.size(); i++) {
        if (std::string arg = args_raw[i]; arg[0] == '-' || arg[0] == '+') {
            starting_script += ' ' + arg;
        } else {
            starting_script += " \"" + arg + "\"";
        }
    }




    // Run the instructions.
    size_t instruction_counter = 0;
    size_t script_counter = 0;
    size_t generation_counter = 0;

    std::vector<std::pair<Token_Type, std::string>> tokens = tokenize_s1(starting_script, {});
    std::vector<Instruction> instructions = parse_s1_file(tokens);
    m1_data active_model = {};
    bool has_active_model = false;
    std::mt19937_64 rng_seeds {std::random_device()()};

    size_t available_instructions = instructions.size();
    while (instruction_counter < available_instructions) {
        const Instruction current_instruction = instructions[instruction_counter];
        switch (current_instruction.type) {

            // Attempt to read the given TSV-file with the given configuration.
            case Instruction_Type::IRead: {
                std::cout << "[" << instruction_counter << "] Reading graph." << std::endl;
                auto tsv_reader = TSVReader(current_instruction.read.node_files, current_instruction.read.edge_files,
                    current_instruction.read.node_name_index, current_instruction.read.node_type_indices,
                    current_instruction.read.start_node_index, current_instruction.read.end_node_index, current_instruction.read.edge_type_indices);
                GenericGraphReader model = {};

                active_model = tsv_reader.readTo(model, current_instruction.read.data, rng_seeds());
                has_active_model = true;
                break;
            }


            // Instructions are parsed from the given script and inserted after the current position in the instruction-queue.
            case Instruction_Type::IExecute: {
                std::cout << "[" << instruction_counter << "] Running script '" << current_instruction.execute.scriptfile_path << "'." << std::endl;
                std::ifstream f(current_instruction.execute.scriptfile_path);
                if (!f.is_open()) {
                    throw std::runtime_error("Could not open file " + current_instruction.execute.scriptfile_path + " for reading.");
                }
                std::stringstream buffer;
                buffer << f.rdbuf();

                std::vector<std::pair<Token_Type, std::string>> new_tokens = tokenize_s1(buffer.str(),
                    current_instruction.execute.replace_templates);
                std::vector<Instruction> new_instructions = parse_s1_file(new_tokens);

                instructions.insert((instructions.begin() + static_cast<long>(instruction_counter) + 1), new_instructions.begin(), new_instructions.end());
                available_instructions = instructions.size();
                ++script_counter;
                break;
            }


            case Instruction_Type::IGenerate: {
                if (!has_active_model) {
                    throw std::runtime_error("A model needs to be active before generation can start. Use -read or -load before generating.");
                }

                size_t to_generate = current_instruction.generate.n_to_generate;
                if (!active_model.meta.values.contains("SCALE")) {
                    active_model.meta.values["SCALE"] = "1.0";
                }


                std::cout << "[" << instruction_counter << "] Generating " << to_generate << " new graph(s) at "
                    << active_model.meta.values["SCALE"] << "x scale." << std::endl;

                if (current_instruction.generate.n_to_generate == 1) {
                    // Single generation is handled separately, as the path does not need to be edited.
                    generate_graph(current_instruction.generate.nodefile_path, current_instruction.generate.edge_file_path, active_model, rng_seeds());
                    std::cout << "\t1.) at '" << current_instruction.generate.nodefile_path << "' and '" << current_instruction.generate.edge_file_path << "'." << std::endl;
                    ++generation_counter;
                } else {
                    // If more than one graph is to be generated, "path/to/filename.ext" is changed to "path/to/filename_i.ext"
                    //      where _i is incremented from 0 to n-1.
                    std::filesystem::path node_path{current_instruction.generate.nodefile_path};
                    std::filesystem::path edge_path{current_instruction.generate.edge_file_path};
                    for (size_t i = 0; i < to_generate; ++i) {
                        std::string n_file = node_path.parent_path().string() + '/' + node_path.stem().string()
                            + '_' + std::to_string(i) + node_path.extension().string();
                        std::string e_file = edge_path.parent_path().string() + '/' + edge_path.stem().string()
                            + '_' + std::to_string(i) + edge_path.extension().string();
                        std::cout << '\t' << (i+1) << ".) at '" << n_file << "' and '" << e_file << "'." << std::endl;
                        generate_graph(n_file, e_file, active_model, rng_seeds());
                        ++generation_counter;
                    }

                }
                break;
            }

            case Instruction_Type::IScale: {
                if (!has_active_model) {
                    throw std::runtime_error("A model needs to be active before it can be scaled. Use -read or -load before scaling.");
                }
                std::cout << "[" << instruction_counter << "] Scaling model by a factor of x" << current_instruction.f_val << "." << std::endl;
                active_model = scale_m1_data(active_model, current_instruction.f_val);
                break;
            }

            case Instruction_Type::ISave: {
                if (!has_active_model) {
                    throw std::runtime_error("A model needs to be active before it can be saved to a file. Use -read or -load before saving.");
                }
                std::cout << "[" << instruction_counter << "] Saving model '" << active_model.meta.name <<"' to '"
                    << current_instruction.s_val << "'." << std::endl;
                size_t bytes_written = write_m1_file(current_instruction.s_val, active_model);
                std::cout << "\tWrote " << bytes_written / 1.0e9L << " GB to the file." << std::endl;
                break;
            }

            case Instruction_Type::ILoad: {
                std::cout << "[" << instruction_counter << "] Reading model from '" << current_instruction.s_val <<"'." << std::endl;
                active_model = read_m1_file(current_instruction.s_val);
                std::cout << "\tActive Model: " << active_model.meta.name << std::endl;
                break;
            }

            case Instruction_Type::ISeed: {
                std::cout << "[" << instruction_counter << "] Setting the random seed to '" << current_instruction.s_val << "'." << std::endl;
                std::seed_seq rng = {current_instruction.s_val.begin(), current_instruction.s_val.end()};
                rng_seeds.seed(rng);
                break;
            }

            case Instruction_Type::IHelp: {
                std::cout << "[" << instruction_counter << "] Displaying program help." << std::endl;
                std::cout << "\tUse double-quotations (\"...\") to retain tabs/spaces/linebreaks within an argument. Instructions are not case-sensitive." << std::endl << std::endl;
                std::cout << "\t### Read a .tsv file, generate an active model in memory. Modify the behaviour of the reader with sub-instructions." << std::endl;
                std::cout << "\t\t-Read" << std::endl;
                std::cout << "\t\t\t+nodefile [nodefile_path1] [nodefile_path2] ..." << std::endl;
                std::cout << "\t\t\t+edgefile [edgefile_path1] [edgefile_path1] ..." << std::endl;
                std::cout << "\t\t\t+nodeindex [index_of_node_name]" << std::endl;
                std::cout << "\t\t\t+nodetypeindex [index_of_node_type1] [index_of_node_type2] ..." << std::endl;
                std::cout << "\t\t\t+edgeindex [index_of_start_node] [index_of_end_node]" << std::endl;
                std::cout << "\t\t\t+edgetypeindex [index_of_edge_type1] [index_of_edge_type2] ..." << std::endl;
                std::cout << "\t\t\t+arg [KEY] [VALUE]" << std::endl << std::endl;

                std::cout << "\t### Execute a script. Non-destructively replaces templates with replaces." << std::endl;
                std::cout << "\t\t-Execute [path_to_script] [template1] [replace1] [template2] [replace2] ..." << std::endl << std::endl;

                std::cout << "\t### Load a model from a file. Set it as the active model." << std::endl;
                std::cout << "\t\t-Load [path_to_model_file]" << std::endl << std::endl;

                std::cout << "\t### Save the currently active model to a file." << std::endl;
                std::cout << "\t\t-Save [model_save_path]" << std::endl << std::endl;

                std::cout << "\t### Scale the currently active model by the given factor. Scaling below x1.0 is not recommended." << std::endl;
                std::cout << "\t\t-Scale [scaling_factor]" << std::endl << std::endl;

                std::cout << "\t### Seed the PRNG used for generation/reading and scaling from this point on." << std::endl;
                std::cout << "\t\t-Seed [seed_string]" << std::endl << std::endl;

                std::cout << "\t### Generate n new graphs from the currently active model at the current scale." << std::endl;
                std::cout << "\t\t-Generate [generated_nodefile_path] [generated_edgefile_path] [number_of_graphs]" << std::endl << std::endl;

                std::cout << "\t### Display this short usage documentation." << std::endl;
                std::cout << "\t\t-Helo" << std::endl;
                break;
            }

            default:
                throw std::runtime_error("Unknown instruction type" + std::to_string(current_instruction.type));
        }

        std::cout << std::endl;
        ++instruction_counter;
    }


    std::cout  << std::endl  << std::endl << "Finished." << std::endl;
    std::cout << instruction_counter << " instruction(s) run." << std::endl;
    std::cout << script_counter << " script(s) calls." << std::endl;
    std::cout << generation_counter << " new graph(s) generated." << std::endl;
}