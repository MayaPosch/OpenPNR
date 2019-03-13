# OpenPNR #

The OpenPNR project aims to provide a convenient way to use open-source FPGA tools, whether as part of a stand-alone toolchain, or integrated in commercial IDEs.

## Current status ##

OpenPNR currently targets the Lattice ECP5 architecture, using components of the [Trellis](https://github.com/SymbiFlow/prjtrellis "Trellis") and [NextPNR](https://github.com/YosysHQ/nextpnr "Nextpnr") projects. It allows for the conversion of the output of a synthesis tool (described in the 'Usage' section) to a bitstream image with which an FPGA can be programmed.

At this point OpenPNR is still in an early phase, with validation of the software underway. Much like the projects which it derives from it is not suitable for production use.


## Building ##

Building OpenPNR requires:

* GCC/MinGW or Clang compiler with C++14 support.
* Boost libraries.
* [POCO](http://pocoproject.org "POCO") libraries.

After extracting or cloning the project to a location on the filesystem, go to the root of the project and execute:

	make ARCH=ecp5

This will build the ECP5 version of the project, producing a binary called `nextpnr-ecp5` in the `bin/` folder.

OpenPNR has been tested on Windows (MSYS2, MinGW 8.x) and Linux (Mint 19.x, GCC).

----------


Individual build options are:
 
* **trellis** 
	* build the Trellis library & tools.
* **import** 	
	* perform all the Trellis database import steps.
* **import_tool** 
	* build the ECP5 import tool.
* **bbasm** 
	* build the bbasm import tool.
* **db_import**
	* create the BBA files for the supported ECP5 device families.
* **db_convert**
	* convert the BBA files to the files that will be compiled into the PNR binary.
* **pnr_build**
	* build the PNR binary.


Keep in mind that building the project requires significant amounts of RAM. The 85k ECP5 device database requires about 4 GB of free RAM.


## Usage ##

Basic usage of the tool in combination with the open source [Yosys Verilog compiler and synthesis tool](https://github.com/YosysHQ/yosys "Yosys") looks as follows, with the basic blinky example:

    all: blinky.bit

	blinky.json: blinky.v
		yosys -p "synth_ecp5 -json blinky.json" blinky.v
	
	blinky_out.config: blinky.json
		nextpnr-ecp5 --json blinky.json --textcfg blinky_out.config --45k
	
	blinky.bit: blinky_out.config
	ecppack blinky_out.config blinky.bit

Here the `nextpnr-ecp5` binary is the one we compiled earlier, and `ecppack` is an utility that is built with the `libtrellis` library. The latter is found together with two other utility tools in the `trellis/bin` folder. 

## License ##

The NextPNR and Trellis components are licensed under their original ISC license. The OpenPNR components are licensed under 3-clause BSD.

