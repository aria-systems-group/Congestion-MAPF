# Congestion MAPF
![test_ubuntu](https://github.com/Jiaoyang-Li/MAPF-LNS2/actions/workflows/test_ubuntu.yml/badge.svg)
![test_macos](https://github.com/Jiaoyang-Li/MAPF-LNS2/actions/workflows/test_macos.yml/badge.svg)

Congestion MAPF: A new optimization metric for large MAPF systems

This repository was originally forked from [this repository](https://github.com/Jiaoyang-Li/MAPF-LNS2).

## Usage
The code requires the external libraries 
BOOST (https://www.boost.org/) and Eigen (https://eigen.tuxfamily.org/). 
Here is an easy way of installing the required libraries on Ubuntu:    
```shell script
sudo apt update
```
- Install the Eigen library (used for linear algebra computing)
    ```shell script
    sudo apt install libeigen3-dev
    ```
- Install the boost library 
    ```shell script
    sudo apt install libboost-all-dev
    ```
    
After you installed both libraries and downloaded the source code, 
go into the directory of the source code and compile it with CMake: 
```shell script
cmake -DCMAKE_BUILD_TYPE=RELEASE .
make
```

Then, you are able to run the code:
```
./lns -m random-32-32-20.map -a random-32-32-20-random-1.scen -o test -k 400 -t 300 --outputPaths=paths.txt 
```

- m: the map file from the MAPF benchmark
- a: the scenario file from the MAPF benchmark
- o: the output file name (no need for file extension)
- k: the number of agents
- t: the runtime limit
- outputPaths: the output file that contains the paths

You can find more details and explanations for all parameters with:
```
./lns --help
```
