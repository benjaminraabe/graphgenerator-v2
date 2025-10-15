/*
 *	Defines the s1-Format. This is used to pass scripts of instructions to the generator.
 *  This interface supports simple templating in scripts. Direct input to the generator from the CLI is treated as a script.
 *
 *   -Read
 *      +nodefile [nodefile_path1] [nodefile_path2] ...
 *      +edgefile [edgefile_path1] [edgefile_path1] ...
 *      +nodeindex [index_of_node_name]
 *      +nodetypeindex [index_of_node_type1] [index_of_node_type2] ...
 *      +edgeindex [index_of_start_node] [index_of_end_node]
 *      +edgetypeindex [index_of_edge_type1] [index_of_edge_type2] ...
 *      +arg [KEY] [VALUE]
 *
 *  -Execute [path_to_script] [template1] [replace1] [template2] [replace2] ...
 *
 *  -Load [path_to_model_file]
 *  -Save [model_save_path]
 *
 *  -Scale [scaling_factor]
 *  -Seed [seed_string]
 *  -Generate [generated_nodefile_path] [generated_edgefile_path] [number_of_graphs]
 *
 *  -Help
 *
 */

#include <vector>
#include <string>
#include <iostream>
#include <bits/ranges_algo.h>

struct Read_Instruction {
    // Path(s) to the data-file(s)
    std::vector<std::string> node_files;
    std::vector<std::string> edge_files;

    // Specify from which columns the edge-data should be read.
    std::size_t node_name_index;    // (Unique) Name of the node
    std::vector<std::size_t> node_type_indices; // Composite-key for the node-type

    // Specify from which columns the edge-data should be read.
    std::size_t start_node_index;   // (Unique) Name of the start-node
    std::size_t end_node_index;     // (Unique) Name of the end-node
    std::vector<std::size_t> edge_type_indices; // Composite-key for the edge-type

    // Addition meta-data for the graph.
    std::map<std::string, std::string> data;
};

struct Generate_Instruction {
    std::string nodefile_path;
    std::string edge_file_path;
    std::size_t n_to_generate;
};

struct Execute_Instruction {
    std::string scriptfile_path;
    std::vector<std::pair<std::string, std::string>> replace_templates;
};

enum Instruction_Type {
    IRead,
    IExecute,
    IGenerate,
    ILoad,
    IScale,
    ISave,
    ISeed,
    IHelp,
    IInfo
};

// I tried to make this a union, the compiler was not impressed. I can live with wasting some space.
struct Instruction {
    Instruction_Type type;
    std::string s_val = "";
    std::float_t f_val = 0.0f;
    Read_Instruction read = {};
    Generate_Instruction generate = {};
    Execute_Instruction execute = {};
};

enum Token_Type {
    TTag,
    TSubtag,
    TArgument
};
enum Escape_Mode {
    Escape_Inactive,
    Escape_Active
};
enum Reader_Mode {
    ReadDefault,
    ReadToken
};


// Replaces all occurrences of search_str in target_str with the value of replace_str. Inplace-Operation, modifies target_str.
//      Some custom logic is implemented to prevent infinite loops, when search_str is a substring of replace_str.
// It is, once again, baffling, that C++ does not provide this in the standard library!
void inplace_replaceAll(const std::string& search_str, const std::string& replace_str, std::string& target_str) {
    const size_t pattern_len = search_str.length();
    const size_t inserted_len = replace_str.length();
    size_t last_pos = 0;

    while (target_str.find(search_str, last_pos) != std::string::npos && last_pos < target_str.length()) {
        size_t pos = target_str.find(search_str, last_pos);
        target_str.replace(pos, pattern_len, replace_str);
        last_pos = pos + inserted_len;
    }
}


// Helper-Function. Compares IS/HAVE number of number rof arguments and type of tag and throws an error if not correct.
void s1_check_parse_valid(const size_t is_arg_count, const size_t want_arg_count,
                          const Token_Type is_t_type, const Token_Type want_t_type, const std::string &name) {
    if (is_arg_count != want_arg_count) {
        throw std::runtime_error("Incorrect number of arguments for " + name + "-instruction. Want: "
            + std::to_string(want_arg_count) + " , Have: " + std::to_string(is_arg_count));
    }
    if (is_t_type != want_t_type) {
        throw std::runtime_error("Incorrect type of tag for " + name + "-instruction. Want: "
            + std::to_string(want_t_type) + " , Have: " + std::to_string(is_t_type));
    }
}


// Tokenize a string of data in the s1-Format. Preparation for later parsing.
std::vector<std::pair<Token_Type, std::string>> tokenize_s1(std::string input,
    const std::vector<std::pair<std::string, std::string>> &replaces) {

    // Apply the replace-operations on all pairs of templates/replaces in the provided order.
    for (const auto &[s_template, s_replace]: replaces) {
        inplace_replaceAll(s_template, s_replace, input);
    }

    Escape_Mode e_mode = Escape_Inactive;
    Reader_Mode r_mode = ReadDefault;

    std::vector<std::string> raw_tokens;
    std::string buffer;
    buffer.reserve(1024);

    for (const char i : input) {
        // Proces Escape-Mode first: Any characters enclosed with " ... " are retained, the enclosing quotes are discarded.
        if (e_mode == Escape_Mode::Escape_Active) {
            if (i == '"') {
                e_mode = Escape_Mode::Escape_Inactive;
                continue;
            }
            buffer.push_back(i);
            continue;
        }

        // In the default Reader-Mode, empty spaces (and similar characters) are discarded.
        // Enter Token-Read-Mode, whenever another character is encountered.
        // Activate Escape-Mode, when a quote is encountered first.
        if (r_mode == Reader_Mode::ReadDefault) {
            if (i == '"') {
                e_mode = Escape_Mode::Escape_Active;
                r_mode = Reader_Mode::ReadToken;
                continue;
            }
            if (i == ' ' || i == '\n' || i == '\r' || i == '\t') {
                continue;
            }
            buffer.push_back(i);
            r_mode = Reader_Mode::ReadToken;
            continue;
        }

        // Read the token until an un-escaped space or linebreak is encountered. Write the token to the result.
        // EOF is handled separately.
        if (r_mode == Reader_Mode::ReadToken) {
            if (i == ' ' || i == '\n' || i == '\r' || i == '\t') {
                raw_tokens.push_back(buffer);
                buffer.clear();
                r_mode = Reader_Mode::ReadDefault;
                continue;
            }
            buffer.push_back(i);
        }

    }
    // If the reader is in escape-mode after the input ends, something went wrong.
    // I decided to just throw an exception instead of warning, instead of continuing with potentially wrong input.
    if (e_mode == Escape_Mode::Escape_Active) {
        throw std::runtime_error("Encountered an unmatched quotation mark. Check your input!");
    }
    // EOF / End-of-string handling of the final token.
    if (!buffer.empty()) {
        raw_tokens.push_back(buffer);
    }

    // Apply specifiers to the tokens. Convert tags and subtags to uppercase to preempt some user-error.
    //      Arguments remain in their original case, to allow case-sensitive file-names (OS-dependant).
    std::vector<std::pair<Token_Type, std::string>> tokens;
    for (auto token: raw_tokens) {
        if (token.starts_with('-')) {
            std::ranges::transform(token, token.begin(), ::toupper);
            tokens.emplace_back(Token_Type::TTag, token);
        } else if (token.starts_with('+')) {
            std::ranges::transform(token, token.begin(), ::toupper);
            tokens.emplace_back(Token_Type::TSubtag, token);
        } else {
            tokens.emplace_back(Token_Type::TArgument, token);
        }
    }
    return tokens;
}


// Parse an already tokenized string in the s1-Format.
// Certain Tags expect a certain structure. Deviations are complained about.
std::vector<Instruction> parse_s1_file(const std::vector<std::pair<Token_Type, std::string>> &tokens) {
    std::vector<Instruction> instructions;

    size_t idx_end_of_instruction = 0;
    size_t current_idx = 0;
    while (current_idx < tokens.size()) {
        if (tokens[current_idx].first == Token_Type::TTag) {
            // Look ahead for the next Tag or End-Of-Buffer, to find all affiliated subtags/arguments
            idx_end_of_instruction = current_idx+1;
            while (idx_end_of_instruction < tokens.size() && tokens[idx_end_of_instruction].first != Token_Type::TTag) {
                ++idx_end_of_instruction;
            }
            --idx_end_of_instruction;

            // Process the tags
            if (tokens[current_idx].second == "-READ") {
                // Create a read-instruction with reasonable default-behaviour. Completeness is checked after parsing the subinstructions.
                    Read_Instruction i = {};
                    i.node_name_index = 0;
                    i.node_type_indices = {1};
                    bool overwritten_default_node_type_index = false;
                    i.start_node_index = 0;
                    i.end_node_index = 1;
                    i.edge_type_indices = {2};
                    bool overwritten_default_edge_type_index = false;

                    // Parse all available sub-instructions.
                    size_t current_idx_sub_instruction = current_idx+1;
                    while (current_idx_sub_instruction <= idx_end_of_instruction) {
                        // Find the last token relating to the current sub-instruction
                        size_t idx_end_of_sub_instruction = current_idx_sub_instruction+1;
                        while (idx_end_of_sub_instruction < tokens.size() &&
                            (tokens[idx_end_of_sub_instruction].first != Token_Type::TSubtag && tokens[idx_end_of_sub_instruction].first != Token_Type::TTag)) {
                            ++idx_end_of_sub_instruction;
                        }
                        --idx_end_of_sub_instruction;

                        // Process the sub-instruction
                        if (tokens[current_idx_sub_instruction].second == "+NODEFILE") {
                            // Append given file-paths to the list of nodefiles. Zero or more arguments can be given.
                            for (size_t arg_idx = current_idx_sub_instruction+1; arg_idx <= idx_end_of_sub_instruction; ++arg_idx) {
                                if (tokens[arg_idx].first == Token_Type::TArgument) {
                                    i.node_files.emplace_back(tokens[arg_idx].second);
                                } else {
                                    throw std::runtime_error("Unexpected sub-instruction. Expected path to node-file. "
                                        + (tokens[arg_idx].first + "@" + tokens[arg_idx].second));
                                }
                            }


                        } else if (tokens[current_idx_sub_instruction].second == "+EDGEFILE") {
                            // Append given file-paths to the list of edgefiles. Zero or more arguments can be given.
                            for (size_t arg_idx = current_idx_sub_instruction+1; arg_idx <= idx_end_of_sub_instruction; ++arg_idx) {
                                if (tokens[arg_idx].first == Token_Type::TArgument) {
                                    i.edge_files.emplace_back(tokens[arg_idx].second);
                                } else {
                                    throw std::runtime_error("Unexpected sub-instruction. Expected path to node-file. "
                                        + (tokens[arg_idx].first + "@" + tokens[arg_idx].second));
                                }
                            }


                        } else if (tokens[current_idx_sub_instruction].second == "+NODEINDEX") {
                            // Allow selection of a column-index, the value of this column is treated as the unique identifier of the node.
                            s1_check_parse_valid(idx_end_of_sub_instruction-current_idx_sub_instruction, 1,
                                         tokens[current_idx_sub_instruction+1].first, Token_Type::TArgument, "+NODEINDEX");
                            size_t idx;
                            try {
                                idx = std::stoul(tokens[current_idx_sub_instruction+1].second);
                            } catch (std::exception &e) {
                                throw std::runtime_error("Could not convert argument '" +
                                    tokens[current_idx_sub_instruction+1].second + "' of NODEINDEX-Instruction to an unsinged int. " + e.what());
                            }
                            i.node_name_index = idx;


                        } else if (tokens[current_idx_sub_instruction].second == "+NODETYPEINDEX") {
                            // Allow selection of one or more column-indices, the values of these columns are appended
                            //      and treated as the type of the node.
                            for (size_t arg_idx = current_idx_sub_instruction+1; arg_idx <= idx_end_of_sub_instruction; ++arg_idx) {
                                if (tokens[arg_idx].first == Token_Type::TArgument) {
                                    size_t idx;
                                    try {
                                        idx = std::stoul(tokens[arg_idx].second);
                                    } catch (std::exception &e) {
                                        throw std::runtime_error("Could not convert argument '" +
                                            tokens[arg_idx].second + "' of NODETYPEINDEX-Instruction to an unsinged int. " + e.what());
                                    }
                                    if (!overwritten_default_node_type_index) {i.node_type_indices.clear();}
                                    i.node_type_indices.emplace_back(idx);
                                    overwritten_default_node_type_index = true;
                                } else {
                                    throw std::runtime_error("Unexpected sub-instruction. Expected path to node-file. "
                                        + (tokens[arg_idx].first + "@" + tokens[arg_idx].second));
                                }
                            }


                        } else if (tokens[current_idx_sub_instruction].second == "+EDGEINDEX") {
                            // Allow selection of two column-indices, the value of this column is treated as the unique
                            //      identifier of the start- and end-node respectively.
                            s1_check_parse_valid(idx_end_of_sub_instruction-current_idx_sub_instruction, 2,
                                         tokens[current_idx_sub_instruction+1].first, Token_Type::TArgument, "+EDGEINDEX");
                            s1_check_parse_valid(idx_end_of_sub_instruction-current_idx_sub_instruction, 2,
                                     tokens[current_idx_sub_instruction+2].first, Token_Type::TArgument, "+EDGEINDEX");

                            size_t idx_s;
                            size_t idx_e;
                            try {
                                idx_s = std::stoul(tokens[current_idx_sub_instruction+1].second);
                            } catch (std::exception &e) {
                                throw std::runtime_error("Could not convert argument '" +
                                    tokens[current_idx_sub_instruction+1].second + "' of EDGEINDEX-Instruction to an unsinged int. " + e.what());
                            }
                            try {
                                idx_e = std::stoul(tokens[current_idx_sub_instruction+2].second);
                            } catch (std::exception &e) {
                                throw std::runtime_error("Could not convert argument '" +
                                    tokens[current_idx_sub_instruction+2].second + "' of EDGEINDEX-Instruction to an unsinged int. " + e.what());
                            }
                            i.start_node_index = idx_s;
                            i.end_node_index = idx_e;


                        } else if (tokens[current_idx_sub_instruction].second == "+EDGETYPEINDEX") {
                            // Allow selection of one or more column-indices, the values of these columns are appended
                            //      and treated as the type of the edge.
                            for (size_t arg_idx = current_idx_sub_instruction+1; arg_idx <= idx_end_of_sub_instruction; ++arg_idx) {
                                if (tokens[arg_idx].first == Token_Type::TArgument) {
                                    size_t idx;
                                    try {
                                        idx = std::stoul(tokens[arg_idx].second);
                                    } catch (std::exception &e) {
                                        throw std::runtime_error("Could not convert argument '" +
                                            tokens[arg_idx].second + "' of EDGETYPEINDEX-Instruction to an unsinged int. " + e.what());
                                    }
                                    if (!overwritten_default_edge_type_index) {i.edge_type_indices.clear();}
                                    i.edge_type_indices.emplace_back(idx);
                                    overwritten_default_edge_type_index = true;
                                } else {
                                    throw std::runtime_error("Unexpected sub-instruction. Expected path to node-file. "
                                        + (tokens[arg_idx].first + "@" + tokens[arg_idx].second));
                                }
                            }


                        } else if (tokens[current_idx_sub_instruction].second == "+ARG") {
                            // Pass additional meta-data to the model. Expects two values, forming a key-value-pair.
                            s1_check_parse_valid(idx_end_of_sub_instruction-current_idx_sub_instruction, 2,
                                         tokens[current_idx_sub_instruction+1].first, Token_Type::TArgument, "+ARG");
                            s1_check_parse_valid(idx_end_of_sub_instruction-current_idx_sub_instruction, 2,
                                     tokens[current_idx_sub_instruction+2].first, Token_Type::TArgument, "+ARG");
                            std::string key = tokens[current_idx_sub_instruction+1].second;
                            std::ranges::transform(key, key.begin(), ::toupper);
                            std::string value = tokens[current_idx_sub_instruction+2].second;
                            i.data[key] = value;


                        } else {
                            throw std::runtime_error("Unexpected token type when parsing the script!"
                                    + (tokens[current_idx_sub_instruction].first + "@" + tokens[current_idx_sub_instruction].second));

                        }

                        // Advance the loop to the next sub-instruction
                        current_idx_sub_instruction = idx_end_of_sub_instruction+1;
                    }
                    Instruction r_instr = {};
                    r_instr.type = Instruction_Type::IRead;
                    r_instr.read = i;
                    instructions.emplace_back(r_instr);


            } else if (tokens[current_idx].second == "-EXECUTE") {
                // Run a s1-script from a given path. Validity/Permission for the given filepath are only checked on execution.
                //      Allows for zero or more template/replacement-pairs, which are substituted, before the script is evaluated.
                //      Circular dependencies are currently NOT checked for.

                // Check for an uneven number of arguments (1 Filepath + 2n Template/Replacement-Arguments)
                if ((idx_end_of_instruction-current_idx < 1) || ((idx_end_of_instruction-current_idx) % 2 != 1)) {
                    throw std::runtime_error("The EXECUTE instruction expects an odd number of arguments: Exactly one filepath and zero or more PAIRS of Template/Replace arguments.");
                }

                if (tokens[current_idx+1].first != Token_Type::TArgument) {
                    throw std::runtime_error("An EXECUTE-instruction must be immediately followed by at least one argument.");
                }

                Execute_Instruction e = {};
                e.scriptfile_path = tokens[current_idx+1].second;

                // Templates/Replacements need to be provided in pairs.
                for (size_t arg_idx = current_idx+2; arg_idx <= idx_end_of_instruction; arg_idx += 2) {
                    if (tokens[arg_idx].first == Token_Type::TArgument && tokens[arg_idx+1].first == Token_Type::TArgument) {
                        e.replace_templates.emplace_back(std::make_pair(tokens[arg_idx].second, tokens[arg_idx+1].second));
                    } else {
                        throw std::runtime_error("Unexpected sub-instruction. Expected pair of Arguments. "
                            + (tokens[arg_idx].first + "@" + tokens[arg_idx].second) + "and"
                                + (tokens[arg_idx].first + "@" + tokens[arg_idx].second));
                    }
                }

                Instruction i = {};
                i.type = Instruction_Type::IExecute;
                i.execute = e;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-LOAD") {
                // Load a model from a given path of a m1-file and set it as the active model. Validity/Permission for the given filepath are only checked on execution.
                s1_check_parse_valid(idx_end_of_instruction-current_idx, 1,
                                         tokens[idx_end_of_instruction].first, Token_Type::TArgument, "LOAD");
                Instruction i = {};
                i.type = Instruction_Type::ILoad;
                i.s_val = tokens[idx_end_of_instruction].second;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-SAVE") {
                // Save the currently active model to a file. Validity/Permission for the given filepath are only checked on execution.
                s1_check_parse_valid(idx_end_of_instruction-current_idx, 1,
                                     tokens[idx_end_of_instruction].first, Token_Type::TArgument, "SAVE");
                Instruction i = {};
                i.type = Instruction_Type::ISave;
                i.s_val = tokens[idx_end_of_instruction].second;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-SCALE") {
                // Scale the currently active model. Scaling is always applied to the relative scale of the current model.
                //      The scaling must be positive, downscaling is permitted but not recommended.
                s1_check_parse_valid(idx_end_of_instruction-current_idx, 1,
                                         tokens[current_idx+1].first, Token_Type::TArgument, "SCALE");
                float scaling_factor = 1;
                try {
                    scaling_factor = std::stof(tokens[current_idx+1].second);
                } catch (std::exception &e) {
                    throw std::runtime_error("Could not convert argument '" +
                        tokens[current_idx+1].second + "' of SCALE-Instruction to a float. " + e.what());
                }
                if (scaling_factor <= 0) {
                    throw std::runtime_error("Scaling factor '" + tokens[current_idx+1].second + "' must be greater than 0");
                }
                Instruction i = {};
                i.type = Instruction_Type::IScale;
                i.f_val = scaling_factor;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-SEED") {
                // Apply a given seed to the PRNG used in Generation.
                s1_check_parse_valid(idx_end_of_instruction-current_idx, 1,
                                         tokens[idx_end_of_instruction].first, Token_Type::TArgument, "LOAD");
                Instruction i = {};
                i.type = Instruction_Type::ISeed;
                i.s_val = tokens[idx_end_of_instruction].second;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-GENERATE") {
                // Generate an instance of the currently active model. Writes the data to the given node/edge-files.
                // If more than 1 instance is to be generated, filenames are appended with the number, i.e. (node_1.tsv, node_2.tsv, ...)
                Generate_Instruction g = {};
                s1_check_parse_valid(idx_end_of_instruction-current_idx, 3,
                                     tokens[current_idx+1].first, Token_Type::TArgument, "GENERATE");
                s1_check_parse_valid(3, 3,
                                     tokens[current_idx+2].first, Token_Type::TArgument, "GENERATE");
                s1_check_parse_valid(3, 3,
                                     tokens[current_idx+3].first, Token_Type::TArgument, "GENERATE");

                g.nodefile_path = tokens[current_idx+1].second;
                g.edge_file_path = tokens[current_idx+2].second;
                try {
                    g.n_to_generate = std::stoul(tokens[current_idx+3].second);
                } catch (std::exception &e) {
                    throw std::runtime_error("Could not convert argument '" +
                        tokens[current_idx+3].second+ "' of GENERATE-Instruction to an unsigned integer. " + e.what());
                }
                Instruction i = {};
                i.type = Instruction_Type::IGenerate;
                i.generate = g;
                instructions.emplace_back(i);


            } else if (tokens[current_idx].second == "-HELP") {
                // Display the program usage documentation.
                Instruction i = {};
                i.type = Instruction_Type::IHelp;
                instructions.emplace_back(i);


            } else {
                throw std::runtime_error("Unknown tag type: " + tokens[current_idx].second);
            }

            // Advance the loop to the next tag
            current_idx = idx_end_of_instruction + 1;

        } else {
            // Should only occur on malformed input. (I.e. Tag/Subtag before the first actual tag.)
            throw std::runtime_error("Unexpected token type when parsing the script!"
                + (tokens[current_idx].first + "@" + tokens[current_idx].second));
        }
    }


    return instructions;
}
