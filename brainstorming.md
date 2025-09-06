# Brainstorming

**NOTE: ChatGPT was used in the completion of this assessment for research, brainstorm, code generation, and debugging purposes. However, official documentation is used as main source of info whenever possible.**

## Firmware
### Understanding Docker
Docker acts as a pre-configured development environment using containers. 
*Containers:*
- Contains everything needed to run application -> no more "works on one machine but not another"
- Runnable instance of an image 
*Images and Dockerfiles:*
- Images are read-only templates for creating a Docker container 
- Dockerfile contains instructions for how image is created
*Workflow looks like:* 
- Edit locally in VS Code.
- Build/test inside the container.

### Project setup
#### Hello World! 
Changed entrypoint in Dockerfile 
Command for building docker container (in root/firmware): 
``` bash
docker compose up
``` 
Container ran, exits with code 0 
#### Hello World 2
Tried modifying main.cpp (print hello world 2 instead of hello world) to verify workflow:
- modify print content in main.cpp
- go back to firmware, ran ```docker compose up --build```
- container ran, printing "hello world, hello again", exits with code 0

Some mistakes along the way: Tried to compile and build through CMake in firmware/build as well as firmware/solution/build

**Key learning: running docker container does all the CMake stuff automatically. To compile and run, simply run docker compose up --build** 

#### Adding dbcppp library as dependency
- Tried to add dbcppp submodule on host, along with .gitmodules at root (firmware)
	- install as per dcbppp library README (didn't work), need to add it as submodule, different from standalone project 
- edit both CMakeLists.txt files: 
	- top level (in firmware): ``` add_subdirectory(${CMAKE_SOURCE_DIR}/external/dbcppp)```
	- in solution/firmware: ``` target_link_libraries(answer PRIVATE dbcppp)```

When docker compose up --build is run, program successfully ran in docker 

### Learn C++
(Have experience in C but not C++)

Object oriented programming
- classes and objects
    - object is an instance of a class 
    - objects contain data and functions that work on the data 

Other notable differences b/w c and c++: 
- vectors: resizable array 
- references: alias for existing variable, created using &variable
    - allows for cleaner function passes, e.g
    ``` c++ 
        void changeValue(int& num) {  // pass by reference
        num = 50;
    }

    int main() {
        int value = 10;
        changeValue(value);   // looks like normal passing
        cout << value;        // prints 50
    }
    ```

### Stage 1
- read line-by-line input from dump.log, extract relevant raw data (timestamp,  interface, CAN id, data payload) for each frame
- it was decided that storing all frames in a vector is unnecessary; chose print-by-line approach 
- set up dbcppp for decoding
-  multiple DBC files may define the same CAN ID by setting up separate ID
    - unique message map for each bus 

- formatted print (printing to terminal for debugging) 
- print to output.txt instead of terminal 
- push solution to stage 1 to Github 

### Stage 2
#### ECU (Electronic Control Units)
- components that control functionality (e.g. engine control unit; transmission; brakes; steering; temperatures)
- any ECU on CAN bus can broadcast info -> all other ECUs accept data, then choose to receive or ignore
microcontroller
    - interprets incoming CAN messages
	- decides what messages to transmit 
- CAN controller (integrated in the mcu)
	- ensures all comms adheres to CAN protocol
- CAN transceiver 
	- connection b/w CAN controller and physical wires 
	- electrical protection

#### CAN 
- communication system used commonly vehicles to enable ECUs w/o a host computer, only CAN nodes 
	- e.g. b/w directly b/w brakes and engine 
- Wiring: all ECUs are connected on a two-wire bus 

Benefits of CAN
- simple & low cost wiring: all nodes (ECUs) communicate via a single system, instead of having dedicated wires b/w each node
	- educes errors and costs
	- reduces weight of vehicle
- popularity of CAN protocol
	- reduces costs  
- one point of entry for all ECUs
	- easy diagnostics, data logging and config 
- robust towards electric disturbances and electromagnetic interference: (EMI) affects both lines in the CAN bus two-wire system equally -> differential signal is robust against environmental noise
	- ideal for safety critical applications (e.g. vehicles)
- collision detection: extensive error detection ensures data integrity 
- improved efficiency: CAN frames are prioritised by ID via arbitration  

#### Chip selection
 Filter out chips by minimum requirements, leaving 21 chips
-> exported to csv with "compare" function on website

Other considerations:
- exceed minimum requirements or just meets them? 
- power requirements
- physical dimensions
- cost

### Stage 3
#### What is unit testing? 
Resource: https://github.com/catchorg/Catch2/blob/devel/docs/tutorial.md

Practical example: test a function that calculates factorials
``` C++
unsigned int Factorial(unsigned int number) {
	return number <= 1 ? number : Factorial(number-1)*number;
} 
```

To test: 

``` C++ 
// header includes Catch2
#include <catch2/catch_test_macros.hpp>

// this is the tested function 
unsigned int Factorial( unsigned int number ) {
    return number <= 1 ? number : Factorial(number-1)*number;
}

// test cases (manually calculated and hardcoded values)
TEST_CASE( "Factorials are computed", "[factorial]" ) {
    REQUIRE( Factorial(1) == 1 );
    REQUIRE( Factorial(2) == 2 );
    REQUIRE( Factorial(3) == 6 );
    REQUIRE( Factorial(10) == 3628800 );
}
```
- when run, executes all cases and reports findings 

#### Catch2 setup
- added root/firmware/solution/tests/test_sample.cpp
- modified CMakeLists.txt

- wrote dummy function to learn catch2 workflow first (root/firmware/solution/tests/test_sample.cpp)

``` c++
#include <catch2/catch_test_macros.hpp>

// Dummy function to test

int dummy(int num) {
    return 0;
}

// test dummy function
TEST_CASE("dummy function returns 0", "[dummy]") {
    REQUIRE(dummy(1) == 0);
    REQUIRE(dummy(2) == 0);
    REQUIRE(dummy(10) == 0);
}
```

#### Unit testing workflow
to "hop into" docker container: 
``` bash
docker compose run --rm -it --entrypoint /bin/bash firmware
```
make sure you're in build, then reconfigure: 
``` bash
rm -rf CMakeCache.txt CMakeFiles
cmake -S .. -B . -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target answer_tests
```
(check to see if tests are up and running)
``` bash
ctest --test-dir solution -N
```
To run test: 
``` bash
ctest --test-dir solution --output-on-failure
```

For now, every time test_sample.cpp is changed, docker container must be rebuilt with: 
``` bash
docker compose up --build
```

#### Refactoring main.cpp
Main.cpp needs to be separated out into function to conduct unit tests. 
- important processes ```parse_frame```, ```load_networks```, ```decode_signals``` were made into functions to improve readability
- smaller helper functions were added and integrated into bigger functions (```pad_payload_8```, ```format_decoded_line```)
- class BusMap added in addition to CanFrame to facilitate unique bus map for each dbcfile
- turn functions into a small library with can_utils.hpp and can_utils.cpp


#### Actually writing tests 