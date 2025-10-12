# graphgenerator-v2
A generator for the directed degree-corrected stochastic-block model. Project for the degree of B.Sc. in computer science.


## Building
It is recommended to compile this project with gcc. Run `cmake` and `make` in the parent directory. An optional dependency on OpenMP is included for multithreading, you can disable this in the `CMakeLists.txt`.


## Usage
You can provide instructions to the program either by calling it over the command line with the appropriate arguments, or by supplying them in a separate script file. Although the syntax is the same for both methods, it is highly recommended that you use script files, as the instructions tend to become rather lengthy. Note that parameters are not case-sensitive, but file paths may be depending on your operating system! 

Currently, only tab-separated value (.tsv) files are permitted as input files. This may change in the future.


### Reading a given graph with `-read`
The graph is defined by two files in .tsv format: a node file and an edge file. A model is produced from the graph and kept in memory as the currently active model. The instruction may be followed by the following sub-instructions:
- `+nodefile [file1] [file2] ...` Specify the file(s) in which the nodes are defined.
- `+edgefile [file1] [file2] ...` Specify the file(s) in which the edges are defined.
- `+nodeindex [idx_of_node_name]` *Optional.* Specify the column in the node file, in which the unique identifier of the node is given. Zero-Indexed. Set to 0 if not given.
- `+edgeindex [idx_of_start_node] [idx_of_end_node]`  *Optional.* Specify the columns in the edge file, in which the unique identifier of the start and end node are given. Zero-Indexed. Set to 0 (start node) and 1 (end node) if not given.
- `+nodetypeindex [idx_of_ntype1] [idx_of_ntype2] ...` *Optional.* Specify the columns in the node file, from which the type of the node is constructed. The final type is constructed by appending the values in the columns in the order given. Zero-Indexed. Set to 1 if not given.
- `+edgetypeindex [idx_of_etype1] [idx_of_etype2] ...` *Optional.* Specify the columns in the edge file, from which the type of the edge is constructed. The final type is constructed by appending the values in the columns in the order given. Zero-Indexed. Set to 2 if not given.
- `+arg [key] [value]` *Optional.* Pass additional data, for example the author, license or a name, to the model-file. Multiple permitted.


### Interacting with models
After reading a graph or loading a model, the latest model is stored in memory and used for operations and generation. You can load a model from a file using `-load [model_path]` and save the latest model using `-save [model_path]`. To scale up a model use the `-scale [scaling_factor]` instruction. Positive decimal values are permitted. Downscaling a model below its original size is generally not recommended, as some statistical guarantees cannot be upheld. Please note that scaling is applied to the current state of the model. For example, if you read a graph and use the commands '-scale 2' and '-scale 5', the model will produce graphs that are 10 times the size of the original.


### Generating instances
Once a model is active (either by reading a graph or loading a model), you can generate instances by using `-generate [nodepath] [edgepath] [number_of_graphs]`. The generated graphs will be written to the provided filepaths, again split into nodes and edges. If the number of graphs to be generated is larger than one, the filepath is appended with `_n.tsv`, where n is counting up from 0.


### Running a script with `-execute [path_to_script] [tpl1] [rpl1] [tpl2] [rpl2] ...`
Script execution supports templating, i.e. you can pass any number of `tpl`/`rpl`-pairs when executing a script and all occurences of the template `tpl` will be replaced with the value of `rpl` before the script is run. **Circular calls are not checked for! Caveat emptor!** \
Let us consider the following script as an example: We read the original graph, save the model to the same folder and generate one new graph twice and ten times the size respectively into the same folder.
```
-read +nodefile "%folder%/test_nodes.tsv" +edgefile "%folder%/test_edges.tsv"
-save "%folder%/test.m1"
-scale 2 -generate "%folder%/test_x2_nodes.tsv" "%folder%/test_x2_edges.tsv" 1
-scale 5 -generate "%folder%/test_x10_nodes.tsv" "%folder%/test_x10_edges.tsv" 1
```
We can now run the script with `-execute "path/to/script.s1" "%folder%" "graph1"` to replace all instances of `%folder%` in our script with `graph1`. The original file is not modified, i.e. we could repeat this with different values for `%folder%`. The percentage-signs bear no special significance, all strings not containing quotes are permitted as templates. It is however recommended to chose some string that does not accidentally collide with other parts of the script.


### Other commands
You can provide a seed to the PRNG using the `-seed [value]` instruction. For a short version of this documentation use the `-help` instruction.


