/*
	trellis_import.cpp - Import ECP5 routing and bels from Project Trellis.
	
	Revision 0
	
	Features:
			- 
			
	Notes:
			- Links with the Trellis static library.
			
	2019/02/02, Maya Posch
*/

#include <ChipConfig.hpp>
#include <Bitstream.hpp>
#include <RoutingGraph.hpp>
#include <Chip.hpp>
#include <Database.hpp>
#include <DedupChipdb.hpp>
#include <Tile.hpp>
#include <iostream>
#include <stdexcept>
#include <streambuf>
#include <fstream>
#include <iomanip>

#include <map>
#include <vector>
#include <regex>
#include <algorithm>
#include <typeinfo>


#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>


// Globals.
std::map<std::string, std::string> dev_names { {"25k", "LFE5UM5G-25F"}, 
												{"45k", "LFE5UM5G-45F"},
												{"85k", "LFE5UM5G-85F"}
											};
													
std::map<std::string, std::string> timing_port_xform {
    {"RAD0", "D0"},
	{"RAD1", "B0"},
	{"RAD2", "C0"},
	{"RAD3", "A0"}
};

std::map<std::string,  int> pip_class_to_idx {{"default", 0}};
std::vector<std::string> speed_grade_names {"6", "7", "8", "8_5G"};


struct Delay {
	int from_pin;
	int to_pin;
	int min_delay;
	int max_delay;
};


struct SetupHold {
	int pin;
	int clock;
	int min_setup;
	int max_setup;
	int min_hold;
	int max_hold;
};


struct PipClass {
	int min_delay;
	int max_delay;
	int min_fanout;
	int max_fanout;
};


struct Cell {
	int celltype;
	std::vector<Delay> delays;
	std::vector<SetupHold> setupholds;
};


struct Pin {
	std::string name;
	Trellis::Location location;
	int bel_index;
};


struct Pins {
	std::vector<Pin> pins;
};


struct PinData {
	Trellis::Location location;
	int bel_index;
	int bank;
	std::string function;
};


struct Chip {
	std::string speed_grade;
	std::vector<Cell> cells;
	std::vector<PipClass> pip_class_delays;
};


struct GlobalInfo {
	int8_t quad;
	int8_t tap_dir;
	int16_t tap_col;
	int16_t spine_col;
	int16_t spine_row;
};


std::map<std::string, uint32_t> constids;
std::map<int, std::map<int, GlobalInfo> > globalInfos;
std::map<std::string, Pins> packages;
std::vector<PinData> pindata;
std::map<std::string, Chip> chips;
int max_row;
int max_col;
std::map<std::string, int> tiletype_names;
std::map<std::string, int> location_types;


// Templates
template <typename T>
T convertVar(Poco::Dynamic::Var var) {
	// Throws a RangeException if the value does not fit into the result variable. 
	// Throws a NotImplementedException if conversion is not available for the given type. 
	// Throws InvalidAccessException if Var is empty.
	T out;
	try {
		out = var.convert<T>();
	}
	catch (Poco::RangeException& e) {
		//
		std::cerr << "Type conversion error: " << e.what() << std::endl;
		try {
			std::cerr << "Input type: " << var.type().name() << std::endl;
		}
		catch (std::exception& e) {
			std::cerr << "Reading RTTI type info on Var input failed." << std::endl;
		}
		
		try {
			std::cerr << "Output type: " << typeid(T).name() << std::endl;
		}
		catch (std::exception& e) {
			std::cerr << "Reading RTTI type info on output type failed." << std::endl;
		}
	}
	
	return out;
}


// Get the index for a tiletype.
int get_tiletype_index(std::string name) {
	//
	if (tiletype_names.find(name) != tiletype_names.end()) {
		// Found the name.
		return tiletype_names[name];
	}
	
	int idx = tiletype_names.size();
	tiletype_names[name] = idx;
	return idx;
}


class BinaryBlobAssembler {
	//
	std::ofstream DB_FILE;

public:
	BinaryBlobAssembler(std::string device) {
		// Open the file, start writing.
		// TODO: We assume that we are being run from a sub-folder of the ecp5 folder.
		DB_FILE.open("../chipdbs/chipdb-" + device + ".bba");
		if (!DB_FILE.is_open()) {
			std::cerr << "Failed to open database file for writing." << "\n";
			return;
		}
	}

    void l(std::string name) { DB_FILE << "label " << name << "\n"; }
	void l(std::string name, std::string ltype) { 
		DB_FILE << "label " << name << " " << ltype << "\n"; 
	}
	
    void r(std::string name) { DB_FILE << "ref " << name << "\n"; }
    void r(std::string name, std::string comment) { 
		DB_FILE << "ref " << name << " " << comment << "\n"; 
	}

    void s(std::string s, std::string comment) {
        //assert "|" not in s
        DB_FILE << "str |" << s << "| " << comment << "\n";
	}

    void u8(int v) { DB_FILE << "u8 " << v << "\n"; }	
    void u8(int v, std::string comment) { DB_FILE << "u8 " << v << " " << comment << "\n"; }
	
    void u16(int v) { DB_FILE << "u16 " << v << "\n"; }
    void u16(int v, std::string comment) { DB_FILE << "u16 " << v << " " << comment << "\n"; }

    void u32(int v) { DB_FILE << "u32 " << v << "\n"; }
    void u32(int v, std::string comment) { DB_FILE << "u32 " << v << " " << comment << "\n"; }

    void pre(std::string s) { DB_FILE << "pre " << s << "\n"; }

    void post(std::string s) { DB_FILE << "post " << s << "\n"; }

    void push(std::string name) { DB_FILE << "push " << name << "\n"; }

    void pop() { DB_FILE << "pop" << "\n"; }
};


void process_timing_data() {
	// 
	
	Poco::JSON::Parser parser;
    for (std::string grade: speed_grade_names) {
		// Create the basic chip instance.
		Chip chip;
		chip.speed_grade = grade;
		
		// Load the JSON file containing the cell data from the Trellis ECP5 folder.
		std::ifstream cellsFile("../../trellis/database/ECP5/timing/speed_" + grade + "/cells.json");
		if (!cellsFile.is_open()) {
			std::cerr << "Failed to open cells file for grade: " << grade << std::endl;
			continue;
		}
		
		parser.reset();
		Poco::Dynamic::Var cellsJson = parser.parse(cellsFile);
		// TODO: validate proper object structure here.
		Poco::JSON::Object::Ptr cellsObject = cellsJson.extract<Poco::JSON::Object::Ptr>();
		
		std::cout << "Processing cell types..." << std::endl;
		
		Poco::JSON::Object::ConstIterator cellit;
		for (cellit = cellsObject->begin(); cellit != cellsObject->end(); ++cellit) {
			std::string cell_name = cellit->first;
			Cell cell;
			
			// Use the name of the cell to get its type. Massage string to make it match.
			std::regex char_re(":|=|,");
			cell_name = std::regex_replace(cell_name, char_re, "_");
			cell.celltype = constids[cell_name];
			
			std::vector<int> delays;
			std::vector<int> setupholds;
			// TODO: test for array.
			Poco::JSON::Array::Ptr cellArr = cellit->second.extract<Poco::JSON::Array::Ptr>();
			for (int index = 0; index < cellArr->size(); ++index) {
				Poco::JSON::Object::Ptr entryObj = cellArr->getObject(index);
				std::string type = entryObj->get("type");
				if (type == "Width") {
					// Nothing to do for this type.
					continue;
				}
				else if (type == "IOPath") {
					//
					std::string from_pin;
					if (entryObj->get("from_pin").isString()) { 
						from_pin = convertVar<std::string>(entryObj->get("from_pin"));
					}
					else {
						from_pin = convertVar<std::string>(entryObj->get("from_pin")[1]);
					}
			
					// Check for the from_pin as key in the timing_port_xform map.
					if (timing_port_xform.find(from_pin) != timing_port_xform.end()) {
						from_pin = timing_port_xform[from_pin];
					}
					
					std::string to_pin = convertVar<std::string>(entryObj->get("to_pin"));
					if (timing_port_xform.find(to_pin) != timing_port_xform.end()) {
						to_pin = timing_port_xform[to_pin];
					}
					
					// Get lowest & highest rising & falling from respective arrays.
					int min_delay = std::min((*(entryObj->getArray("rising"))).getElement<int>(0), 
											(*(entryObj->getArray("falling"))).getElement<int>(0));
                    int max_delay = std::min((*(entryObj->getArray("rising"))).getElement<int>(2), 
											(*(entryObj->getArray("falling"))).getElement<int>(2));
					
					Delay delay;
					delay.from_pin = constids[from_pin];
					delay.to_pin = constids[to_pin];
					delay.min_delay = constids[std::to_string(min_delay)];
					delay.max_delay = constids[std::to_string(max_delay)];
					cell.delays.push_back(delay);
				}
				else if (type == "SetupHold") {
					// Read in the setup hold values.
					SetupHold setuphold;
					setuphold.pin = constids[convertVar<std::string>(entryObj->get("pin"))];
					setuphold.clock = constids[convertVar<std::string>((*(entryObj->getArray("clock"))).getElement<std::string>(1))];
					setuphold.min_setup = (*(entryObj->getArray("setup"))).getElement<int>(0);
                    setuphold.max_setup = (*(entryObj->getArray("setup"))).getElement<int>(2);
                    setuphold.min_hold = (*(entryObj->getArray("hold"))).getElement<int>(0);
                    setuphold.max_hold = (*(entryObj->getArray("hold"))).getElement<int>(2);
					cell.setupholds.push_back(setuphold);
				}
				else {
					std::cerr << "Invalid cell data type: " << type << std::endl;
					exit(1);
				}
			}
			
			// 	Move the cell instance 			
			chip.cells.push_back(std::move(cell));
		}
		
		std::cout << "Done." << std::endl;
		std::cout << "Setting up PIP class to Index." << std::endl;
		
		int pip_len = pip_class_to_idx.size();
		for (int i = 0; i < pip_len; ++i) {
			PipClass pipclass;
			pipclass.min_delay = 50;
			pipclass.max_delay = 50;
			pipclass.min_fanout = 0;
			pipclass.max_fanout = 0;
			chip.pip_class_delays.push_back(pipclass);
		}
		
		std::cout << "Loading interconnect file..." << std::endl;
		
		// Load the JSON file containing the cell data from the Trellis ECP5 folder.
		std::ifstream inconFile("../../trellis/database/ECP5/timing/speed_" + grade 
																		+ "/interconnect.json");
		if (!inconFile.is_open()) {
			std::cerr << "Failed to open interconnect file for grade: " << grade << std::endl;
			continue;
		}
		
		Poco::JSON::Parser inconParser;
		Poco::Dynamic::Var inconJson = inconParser.parse(inconFile);
		Poco::JSON::Object::Ptr inconObject = inconJson.extract<Poco::JSON::Object::Ptr>();
		
		std::cout << "Processing interconnections..." << std::endl;
		
		Poco::JSON::Object::ConstIterator inconit;
		for (inconit = inconObject->begin(); inconit != inconObject->end(); ++inconit) {
			std::string pipclass = inconit->first;
			Poco::JSON::Object::Ptr itemObj = inconit->second.extract<Poco::JSON::Object::Ptr>();
			Poco::JSON::Array::Ptr delayArr = itemObj->getArray("delay");
			Poco::JSON::Array::Ptr fanoutArr = itemObj->getArray("fanout");
			int min_delay = convertVar<int>((*delayArr).getElement<int>(0));
			min_delay *= 1.1;
            int max_delay = convertVar<int>((*delayArr).getElement<int>(2));
			max_delay *=  1.1;
            int min_fanout = convertVar<int>((*fanoutArr).getElement<int>(0));
            int max_fanout = convertVar<int>((*fanoutArr).getElement<int>(0));
			if (grade == "6") {
				PipClass pc;
				pip_class_to_idx[pipclass] = chip.pip_class_delays.size();
				//std::cout << "Adding pipclass_to_idx '" << pipclass << "' with size: " << chip.pip_class_delays.size() << std::endl;
				pc.min_delay = min_delay;
				pc.max_delay = max_delay;
				pc.min_fanout = min_fanout;
				pc.max_fanout = max_fanout;
				chip.pip_class_delays.push_back(pc);
			}
			else {
				//
				if (pip_class_to_idx.find(pipclass) != pip_class_to_idx.end()) {
					// Overwrite.
					PipClass pc;
					pc.min_delay = min_delay;
					pc.max_delay = max_delay;
					pc.min_fanout = min_fanout;
					pc.max_fanout = max_fanout;
					chip.pip_class_delays[pip_class_to_idx[pipclass]] = pc;
				}
			}
		}
		
		std::cout << "Done." << std::endl;
		
		chips.insert(std::pair<std::string, Chip>(grade, chip));
	}
}


int get_bel_index(std::shared_ptr<Trellis::DDChipDb::DedupChipdb> ddrg, Trellis::Location loc, std::string name) {
	Trellis::DDChipDb::LocationData locData = (*ddrg).locationTypes[(*ddrg).typeAtLocation[loc]];
	int idx = 0;
	for (Trellis::DDChipDb::BelData bel : locData.bels) {
		//
		if ((*ddrg).to_str(bel.name) == name) {
			return idx;
		}
		
		idx++;
	}
		
	// TODO: Perform check?
	//if (loc.y != max_row) { /* error */ }
	return -1;
}


void process_pio_db(std::shared_ptr<Trellis::DDChipDb::DedupChipdb> ddrg, std::string device) {
	//
	// Load the JSON file containing the IO data from the Trellis ECP5 folder.
	std::ifstream ioFile("../../trellis/database/ECP5/" + dev_names[device] + "/iodb.json");
	if (!ioFile.is_open()) {
		std::cerr << "Failed to open IODB file for device: " << device << std::endl;
		return;
	}
	
	Poco::JSON::Parser parser;
	Poco::Dynamic::Var ioJson = parser.parse(ioFile);
	Poco::JSON::Object::Ptr ioObject = ioJson.extract<Poco::JSON::Object::Ptr>();
	
	
	std::cout << "Parsing IODB JSON..." << std::endl;
	
	Poco::JSON::Object::ConstIterator ioit;
	Poco::JSON::Object::Ptr pkgObj = ioObject->getObject("packages");
	for (ioit = pkgObj->begin(); ioit != pkgObj->end(); ++ioit) {
		//
		Pins pins;
		Poco::JSON::Object::Ptr valObj = ioit->second.extract<Poco::JSON::Object::Ptr>();
		Poco::JSON::Object::ConstIterator valit;
		for (valit = valObj->begin(); valit != valObj->end(); ++valit) {
			Poco::JSON::Object::Ptr itemObj = valit->second.extract<Poco::JSON::Object::Ptr>();
			int x = itemObj->getValue<int>("col");
            int y = itemObj->getValue<int>("row");
			Trellis::Location loc = Trellis::Location(x, y);
			std::string pio = "PIO" + itemObj->getValue<std::string>("pio");
			int bel_idx = get_bel_index(ddrg, loc, pio);
			if (bel_idx != -1) {
				Pin pin;
				pin.name = ioit->first;
				pin.location = loc;
				pin.bel_index = bel_idx;
				pins.pins.push_back(std::move(pin));
			}
		}
				
		packages[ioit->first] = pins;
	}
	
	std::cout << "Parsing PIO metadata..." << std::endl;
	
	Poco::JSON::Array::Ptr pioArr = ioObject->getArray("pio_metadata");
	for (int index = 0; index < pioArr->size(); ++index) {
		//
		Poco::JSON::Object::Ptr objPtr = pioArr->getObject(index);
		int x = objPtr->getValue<int>("col");
		int y = objPtr->getValue<int>("row");
		Trellis::Location loc = Trellis::Location(x, y);
		std::string pio = "PIO" + objPtr->getValue<std::string>("pio");
		int bank = objPtr->getValue<int>("bank");
		std::string pinfunc;
		if (objPtr->has("function")) {
			pinfunc = objPtr->getValue<std::string>("function");
		}
		
		int bel_idx = get_bel_index(ddrg, loc, pio);
		if (bel_idx != -1) {
			//
			PinData pd;
			pd.location = loc;
			pd.bel_index = bel_idx;
			pd.bank = bank;
			pd.function = pinfunc;
			pindata.push_back(std::move(pd));
		}
	}
}


std::map<std::string, int> quadrants { {"UL", 0}, {"UR", 1}, {"LL", 2}, {"LR", 3} };

void process_loc_globals(Trellis::Chip chip) {
	//
    max_row = chip.get_max_row();
    max_col = chip.get_max_col();
	for (int y = 0; y < (max_row + 1); ++y) {
		std::map<int, GlobalInfo> gi_row;
		for (int x = 0; x < (max_col + 1); ++x) {
			//
			GlobalInfo gi;
			std::string quad = chip.global_data.get_quadrant(y, x);
			Trellis::TapDriver tapdrv = chip.global_data.get_tap_driver(y, x);
			std::pair<int, int> spine;
			if (tapdrv.col == x) {
				//
				std::pair<int, int> spinedrv = chip.global_data.get_spine_driver(quad, x);
				gi.spine_col = spinedrv.second;
				gi.spine_row = spinedrv.first;
			}
			else {
				//
				gi.spine_col = -1;
				gi.spine_row = -1;
			}
			
			// 
			gi.quad = quadrants.at(quad);
			gi.tap_col = (int16_t) tapdrv.LEFT;
			gi.tap_dir = (int16_t) tapdrv.RIGHT;
			gi_row.insert(std::pair<int, GlobalInfo>(x, std::move(gi)));
		}
		
		globalInfos.insert(std::pair<int, std::map<int, GlobalInfo> >(y, std::move(gi_row)));
	}
}


void write_loc(BinaryBlobAssembler &bba, Trellis::Location loc, std::string sym_name) {
	//
	bba.u16(loc.x, sym_name + ".x");
    bba.u16(loc.y, sym_name + ".y");
}


std::map<int, std::pair<int, int> > loc_with_type;

std::string get_wire_name(std::shared_ptr<Trellis::DDChipDb::DedupChipdb> ddrg, int arc_loctype, Trellis::Location rel, int32_t idx) {
	std::map<int, std::pair<int, int> >::iterator it = loc_with_type.find(arc_loctype);
	if (it == loc_with_type.end()) { return std::string(); }
	
	// Debug.
	//std::cout << "arc_loctype: " << arc_loctype << ", index: " << idx << std::endl;
	
    std::pair<int, int> loc = loc_with_type[arc_loctype];
	Trellis::DDChipDb::checksum_t lt = (*ddrg).typeAtLocation[Trellis::Location(loc.first + rel.x, loc.second + rel.y)];
	Trellis::DDChipDb::WireData wiredata = (*ddrg).locationTypes[lt].wires[idx];
	
	// Debug
	//std::cout << "loc: " << loc.first << ", " << loc.second << std::endl;
	//std::cout << "rel: " << rel.x << ", " << rel.y << std::endl;
	
	std::string out;
	out += "R";
	out += std::to_string(loc.second + rel.y);
	out += "C";
	out += std::to_string(loc.first + rel.x);
	out += "_";
	out += (*ddrg).to_str(wiredata.name);
	return out;
}


class StringRef {
    char const* begin_;
    int size_;

public:
    int size() const { return size_; }
    char const* begin() const { return begin_; }
    char const* end() const { return begin_ + size_; }

    StringRef(char const* const begin, int const size) : begin_(begin), size_(size) {}
};


std::vector<StringRef> split(std::string const& str, char delimiter = ' ', int limit = -1) {
    std::vector<StringRef> result;
    enum State { inSpace, inToken };
	
	int count = 0;
    State state = inSpace;
    char const* pTokenBegin = 0;
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        State const newState = (*it == delimiter ? inSpace : inToken);
        if (newState != state) {
            if (newState == inSpace) {
				result.push_back(StringRef(pTokenBegin, &*it - pTokenBegin));				
				if (limit != -1) {
					count++;
					if (count == limit) {
						it++; // advance to the next character.
						pTokenBegin = &*it;
						state = inToken;
						break;
					}						
				}					
			}
			else if (newState == inToken) {
				pTokenBegin = &*it;
			}
        }
		
        state = newState;
    }
	
    if (state == inToken) {
        result.push_back(StringRef(pTokenBegin, &*str.end() - pTokenBegin));
    }
	
    return result;
}


static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}


inline bool endsWith(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    }
        
	return false;
}


bool is_denorm(std::string wire) {
    if ((wire.compare(0, 3,"H06") == 0 || wire.compare(0, 3, "V06") == 0) && !(endsWith(wire, "03"))) {
        return true;
	}
	
    if ((wire.compare(0, 3,"H02") == 0 || wire.compare(0, 3,"V02") == 0) && !(endsWith(wire, "01"))) {
        return true;
	}
	
    return false;
}


std::string get_span(std::string wire) {
    if ((wire.compare(0, 1, "H") == 0 || wire.compare(0, 1, "V") == 0) && is_digit(wire[1]) 
																&& is_digit(wire[2])) {
		char w0 = wire[0];
		char w3 = wire[3];
		std::string out;
		out += "span";
		out += wire[2];
		out += (char) tolower(w0);
		out += (char) tolower(w3);
		return out;
	}
	
    return std::string();	
}


std::pair<int, int> pos_from_name(std::string &tile, int chip_size_x, int chip_size_y, int bias) {
	// Extract the tile position as a (row, column) tuple from its name.
    std::pair<int, int> size;
    size.first = chip_size_x;
    size.second = chip_size_y;

    std::pair<int, int> pos = Trellis::get_row_col_pair_from_chipsize(tile, size, bias);
    return pos;
}


std::string format_rel(std::string &a, std::string &b) {
    std::pair<int, int> rca = pos_from_name(a, 126, 95, 0);
    std::pair<int, int> rcb = pos_from_name(b, 126, 95, 0);
	
	// Debug
	//std::cout << "RCA: " << rca.first << ", " << rca.second << std::endl;
	//std::cout << "RCB: " << rcb.first << ", " << rcb.second << std::endl;
	
    std::string rel;
    if (rcb.first < rca.first) {
        rel += "n";
		rel += std::to_string(rca.first - rcb.first);
	}
    else if (rcb.first > rca.first) {
        rel += "s"; 
		rel += std::to_string(rcb.first - rca.first);
	}

    if (rcb.second < rca.second) {
        rel += "w"; 
		rel += std::to_string(rca.second - rcb.second);
	}
    else if (rcb.second > rca.second) {
        rel += "e"; 
		rel += std::to_string(rcb.second - rca.second);
	}

    if (!rel.empty()) {
        rel = "_" + rel;
	}
	
    return rel;
}


std::string get_pip_class_name(std::string &source, std::string &sink) {
	std::vector<StringRef> sourceBits = split(source, '_', 1);
	std::string source_loc = std::string(sourceBits[0].begin(), sourceBits[0].end());
	std::string source_base = std::string(sourceBits[1].begin(), sourceBits[1].end());
	
	std::vector<StringRef> sinkBits = split(sink, '_', 1);
	std::string sink_loc = std::string(sinkBits[0].begin(), sinkBits[0].end());
	std::string sink_base = std::string(sinkBits[1].begin(), sinkBits[1].end());
	
	//std::cout << "Get PIP Class Name: " << source_loc << ", " << source_base << std::endl;
	//std::cout << "Get PIP Class Name: " << sink_loc << ", " << sink_base << std::endl;
	
    if (is_denorm(source_base) || is_denorm(sink_base)) {
        return std::string();
	}
	
    if (endsWith(source_base, "_SLICE") || source_base.compare(0, 3, "MUX") == 0 
										|| endsWith(sink_base, "_SLICE")) {
        return "slice_internal";
	}
	
    if (endsWith(sink_base, "_EBR") || endsWith(source_base, "_EBR")) {
        return "ebr_internal";
	}
	
    if (sink_base.find("TEST") != std::string::npos || 
		source_base.find("TEST") != std::string::npos) {
        return std::string();
	}
	
    if (sink_base.find("ALU") != std::string::npos || 
			source_base.find("ALU") != std::string::npos || 
			sink_base.find("MULT") != std::string::npos || 
			source_base.find("MULT") != std::string::npos ||
			sink_base.find("PRADD") != std::string::npos) {
        return "dsp_internal";
	}
	
	const std::regex lc_input_re("(J?[ABCDM]|CLK|LSR|CE)\\d");
	const std::regex lc_output_re("J?[FQ]\\d");
	
    if (std::regex_match(sink_base, lc_input_re)) {
		//std::cout << "Match LC Input RegEx." << std::endl;
        if (std::regex_match(source_base, lc_output_re)) {
			//std::cout << "Match LC Output RegEx." << std::endl;
			std::string out;
			source_base.pop_back();
			std::transform(source_base.begin(), source_base.end(), source_base.begin(), ::tolower);
			sink_base.pop_back();
			std::transform(sink_base.begin(), sink_base.end(), sink_base.begin(), ::tolower);
			out += source_base;
			out += "_to_";
			out += sink_base;
            return out;
		}
		else if (!(get_span(source_base).empty())) {
			sink_base.pop_back();
			std::transform(sink_base.begin(), sink_base.end(), sink_base.begin(), ::tolower);
            std::string out = get_span(source_base);
			out += "_to_"; 
			out += sink_base;
			out += format_rel(source_loc, sink_loc);
			return out;
		}
        else if (source_base.find("HPBX") != std::string::npos) {
			sink_base.pop_back();
			std::transform(sink_base.begin(), sink_base.end(), sink_base.begin(), ::tolower);
			std::string out = "global_to_";
			out += sink_base;
			return out;
		}
        else {
            return std::string();
		}
	}
    else if (!(get_span(sink_base).empty())) {
		//std::cout << "Sink base span not empty." << std::endl;
        if (std::regex_match(source_base, lc_output_re)) {
			source_base.pop_back();
			std::transform(source_base.begin(), source_base.end(), source_base.begin(), ::tolower);
			std::string out;
			out += source_base;
			out += "_to_";
			out += get_span(sink_base);
			out += format_rel(source_loc, sink_loc);
			return out;
		}
        else if (!get_span(source_base).empty()) {
            std::string out;
			out += get_span(source_base);
			out += "_to_";
			out += get_span(sink_base);
			out += format_rel(source_loc, sink_loc);
			return out;
		}
        else if (source_base.find("HPBX") != std::string::npos) {
            return "global_to_" + get_span(sink_base);
		}
        else if (source_base.find("BOUNCE") != std::string::npos) {
            return std::string();
		}
        else {
			// TODO: handle.
            //assert False, (source, sink)
		}
	}
    else if ((source_base.compare(0, 3, "LSR") == 0) && 
				(sink_base.compare(0, 6, "MUXLSR") == 0)) {
		//std::cout << "Match LSR & MUXSLR." << std::endl;
        return "lsr_to_muxlsr";
	}
		
	return std::string();
}


int get_pip_class(std::string &wire_from, std::string &wire_to) {
	// Debug
	//std::cout << "Request for PIP class. Wire from: " << wire_from << ", to: " << wire_to << std::endl;
	
    std::string class_name = get_pip_class_name(wire_from, wire_to);
	
	//std::cout << "Class name: " << class_name << std::endl;
	
    if (!class_name.empty() && pip_class_to_idx.find(class_name) == pip_class_to_idx.end()) {
        class_name = "default";
	}
		
    return pip_class_to_idx[class_name];
}


void write_database(std::string device_name, Trellis::Chip chip, 
					std::shared_ptr<Trellis::DDChipDb::DedupChipdb> ddrg, std::string endianness) {
	// For each device a database file is written to disk. 
	// Database location and name is: <nextpnr root>/ecp5/chipdbs/chipdb-<device>.bba
	// For ECP5 the devices supported are: 25k, 45k and 85k.
	// TODO: implement the direct generation function.
	BinaryBlobAssembler bba(device_name);
		
	//
	bba.pre("#include \"nextpnr.h\"");
    bba.pre("NEXTPNR_NAMESPACE_BEGIN");
    bba.post("NEXTPNR_NAMESPACE_END");
    bba.push("chipdb_blob_" + device_name);
    bba.r("chip_info", "chip_info");

	// Get the keys from the location data map.
	std::vector<Trellis::DDChipDb::checksum_t> loctypes;
	loctypes.reserve((*ddrg).locationTypes.size());
	for (std::pair<Trellis::DDChipDb::checksum_t, Trellis::DDChipDb::LocationData> imap : (*ddrg).locationTypes) {
		loctypes.push_back(imap.first);
	}
	
	// Checksum keys get combined with x/y locations.
    for (int y = 0; y < max_row + 1; ++y) {
        for (int x = 0; x < max_col + 1; ++x) {
			std::ptrdiff_t pos = std::distance(loctypes.begin(), 
								std::find(loctypes.begin(), loctypes.end(), 
								(*ddrg).typeAtLocation[Trellis::Location(x, y)]));
			if (pos >= loctypes.size()) {
				// Not found.
				std::cerr << "write_database: Checksum key not found." << std::endl;
				continue;
			}
			
            loc_with_type[pos] = std::pair<int, int>(x, y);
		}
	}

	std::cout << "Writing location types..." << std::endl;
	
	// Debug
	std::cout << "Found " << loctypes.size() << " entries in loctypes vector." << std::endl;
	
    for (int idx = 0; idx < loctypes.size(); ++idx) {		
        Trellis::DDChipDb::LocationData loctype = (*ddrg).locationTypes[loctypes[idx]];
        if (loctype.arcs.size() > 0) {
            bba.l("loc" + std::to_string(idx) + "_pips", "PipInfoPOD");
            for (Trellis::DDChipDb::DdArcData arc : loctype.arcs) {
                write_loc(bba, arc.srcWire.rel, "src");
                write_loc(bba, arc.sinkWire.rel, "dst");
                bba.u32(arc.srcWire.id, "src_idx");
                bba.u32(arc.sinkWire.id, "dst_idx");
                std::string src_name = get_wire_name(ddrg, idx, arc.srcWire.rel, arc.srcWire.id);
                std::string snk_name = get_wire_name(ddrg, idx, arc.sinkWire.rel, arc.sinkWire.id);
                bba.u32(get_pip_class(src_name, snk_name), "timing_class");
                bba.u16(get_tiletype_index((*ddrg).to_str(arc.tiletype)), "tile_type");
                Trellis::DDChipDb::ArcClass cls = arc.cls;
				if (cls == Trellis::DDChipDb::ARC_STANDARD && 
								(snk_name.find("PCS") != std::string::npos) || 
								(snk_name.find("DCU") != std::string::npos) || 
								(src_name.find("DCU") != std::string::npos)) {
                   cls = Trellis::DDChipDb::ARC_FIXED;
				}
				
                bba.u8(cls, "pip_type");
                bba.u8(0, "padding");
			}
		}
		
		int locwirelen = loctype.wires.size();
        if (locwirelen > 0) {
            for (int wire_idx = 0; wire_idx < locwirelen; ++wire_idx) {
                Trellis::DDChipDb::WireData wire = loctype.wires[wire_idx];
                if (wire.arcsDownhill.size() > 0) {
                    bba.l("loc" + std::to_string(idx) + "_wire" 
								+ std::to_string(wire_idx) + "_downpips", "PipLocatorPOD");
                    for (Trellis::DDChipDb::RelId dp : wire.arcsDownhill) {
                        write_loc(bba, dp.rel, "rel_loc");
                        bba.u32(dp.id, "index");
					}
				}
				
                if (wire.arcsUphill.size() > 0) {
                    bba.l("loc" + std::to_string(idx) + "_wire" + std::to_string(wire_idx) 
												+ "_uppips", "PipLocatorPOD");
                    for (Trellis::DDChipDb::RelId up : wire.arcsUphill) {
                        write_loc(bba, up.rel, "rel_loc");
                        bba.u32(up.id, "index");
					}
				}
				
                if (wire.belPins.size() > 0) {
                    bba.l("loc" + std::to_string(idx) + "_wire" + std::to_string(wire_idx) 
																+ "_belpins", "BelPortPOD");
                    for (Trellis::DDChipDb::BelPort bp : wire.belPins) {
                        write_loc(bba, bp.bel.rel, "rel_bel_loc");
                        bba.u32(bp.bel.id, "bel_index");
                        bba.u32(constids[(*ddrg).to_str(bp.pin)], "port");
					}
				}
			}
			
            bba.l("loc" + std::to_string(idx) + "_wires", "WireInfoPOD");
            for (int wire_idx = 0; wire_idx < loctype.wires.size(); ++wire_idx) {
                Trellis::DDChipDb::WireData wire = loctype.wires[wire_idx];
                bba.s((*ddrg).to_str(wire.name), "name");
                bba.u32(wire.arcsUphill.size(), "num_uphill");
                bba.u32(wire.arcsDownhill.size(), "num_downhill");
				if (wire.arcsUphill.size() > 0) {
					bba.r("loc" + std::to_string(idx) + "_wire" + std::to_string(wire_idx) 	
							+ "_uppips", "pips_uphill");
				}
				else {
					bba.r("None", "pips_uphill");
				}
				
				if (wire.arcsDownhill.size() > 0) {
					bba.r("loc" + std::to_string(idx) + "_wire" + std::to_string(wire_idx) 	
							+ "_downpips", "pips_downhill");
				}
				else {
					bba.r("None", "pips_downhill");
				}
				
                bba.u32(wire.belPins.size(), "num_bel_pins");
				if (wire.belPins.size() > 0) {
					bba.r("loc" + std::to_string(idx) + "_wire" + std::to_string(wire_idx) 	
							+ "_belpins", "bel_pins");
				}
				else {
					bba.r("None", "bel_pins");
				}
			}
		}

        if (loctype.bels.size() > 0) {
            for (int bel_idx = 0; bel_idx < loctype.bels.size(); ++bel_idx) {
                Trellis::DDChipDb::BelData bel = loctype.bels[bel_idx];
                bba.l("loc" + std::to_string(idx) + "_bel" + std::to_string(bel_idx) + "_wires",
																					"BelWirePOD");
                for (Trellis::DDChipDb::BelWire pin : bel.wires) {
                    write_loc(bba, pin.wire.rel, "rel_wire_loc");
                    bba.u32(pin.wire.id, "wire_index");
                    bba.u32(constids[(*ddrg).to_str(pin.pin)], "port");
                    bba.u32(int(pin.dir), "dir");
				}
			}
			
            bba.l("loc" + std::to_string(idx) + "_bels", "BelInfoPOD");
            for (int bel_idx = 0; bel_idx < loctype.bels.size(); ++bel_idx) {
                Trellis::DDChipDb::BelData bel = loctype.bels[bel_idx];
                bba.s((*ddrg).to_str(bel.name), "name");
                bba.u32(constids[(*ddrg).to_str(bel.type)], "type");
                bba.u32(bel.z, "z");
                bba.u32(bel.wires.size(), "num_bel_wires");
                bba.r("loc" + std::to_string(idx) + "_bel" + std::to_string(bel_idx) 
															+ "_wires", "bel_wires");
			}
		}
	}

	std::cout << "Writing location type POD..." << std::endl;
	
    bba.l("locations", "LocationTypePOD");
    for (int idx = 0; idx < loctypes.size(); ++idx) {
        Trellis::DDChipDb::LocationData loctype = (*ddrg).locationTypes[loctypes[idx]];
        bba.u32(loctype.bels.size(), "num_bels");
        bba.u32(loctype.wires.size(), "num_wires");
        bba.u32(loctype.arcs.size(), "num_pips");
		if (loctype.bels.size() > 0) {
			bba.r("loc" + std::to_string(idx) + "_bels", "bel_data");
		}
		else {
			bba.r("None", "bel_data");
		}
		
		if (loctype.wires.size() > 0) {
			bba.r("loc" + std::to_string(idx) + "_wires", "wire_data");
		}
		else {
			bba.r("None", "wire_data");
		}
		
		if (loctype.arcs.size() > 0) {
			bba.r("loc" + std::to_string(idx) + "_pips", "pips_data");
		}
		else {
			bba.r("None", "pips_data");
		}
	}

    for (int y = 0; y < max_row + 1; ++y) {
        for (int x = 0; x < max_col + 1; ++x) {
            bba.l("tile_info_" + std::to_string(x) + "_" + std::to_string(y), "TileNamePOD");
            for (std::shared_ptr<Trellis::Tile> tile : chip.get_tiles_by_position(y, x)) {
                bba.s((*tile).info.name, "name");
                bba.u16(get_tiletype_index((*tile).info.type), "type_idx");
                bba.u16(0, "padding");
			}
		}
	}
	
	std::cout << "Writing tile info..." << std::endl;

    bba.l("tiles_info", "TileInfoPOD");
    for (int y = 0; y < max_row + 1; ++y) {
        for (int x = 0; x < max_col + 1; ++x) {
            bba.u32((chip.get_tiles_by_position(y, x)).size(), "num_tiles");
            bba.r("tile_info_" + std::to_string(x) + "_" + std::to_string(y), "tile_names");
		}
	}
	
	std::cout << "Writing location types..." << std::endl;

    bba.l("location_types", "int32_t");
    for (int y = 0; y < max_row + 1; ++y) {
        for (int x = 0; x < max_col + 1; ++x) {
			std::ptrdiff_t pos = std::distance(loctypes.begin(), 
								std::find(loctypes.begin(), loctypes.end(), 
								(*ddrg).typeAtLocation[Trellis::Location(x, y)]));
			if (pos >= loctypes.size()) {
				// Not found.
				std::cerr << "write_database: Checksum key not found." << std::endl;
				continue;
			}
			
            bba.u32(pos, "loctype");
		}
	}
	
	std::cout << "Writing global info..." << std::endl;

    bba.l("location_glbinfo", "GlobalInfoPOD");
    for (int y = 0; y < max_row + 1; ++y) {
        for (int x = 0; x < max_col + 1; ++x) {
            bba.u16(globalInfos[x][y].tap_col, "tap_col");
            bba.u8(globalInfos[x][y].tap_dir, "tap_dir");
            bba.u8(globalInfos[x][y].quad, "quad");
            bba.u16(globalInfos[x][y].spine_row, "spine_row");
            bba.u16(globalInfos[x][y].spine_col, "spine_col");
		}
	}

    for (auto& [package, pkgdata] : packages) {
        bba.l("package_data_" + package, "PackagePinPOD");
        for (Pin pin : pkgdata.pins) {
            bba.s(pin.name, "name");
            write_loc(bba, pin.location, "abs_loc");
            bba.u32(pin.bel_index, "bel_index");
		}
	}
	
	std::cout << "Writing package info..." << std::endl;

    bba.l("package_data", "PackageInfoPOD");
    for (auto& [package, pkgdata] : packages) {
        bba.s(package, "name");
        bba.u32(pkgdata.pins.size(), "num_pins");
        bba.r("package_data_" + package, "pin_data");
	}
	
	std::cout << "Writing PIO info..." << std::endl;

    bba.l("pio_info", "PIOInfoPOD");
    for (PinData pin : pindata) {
        write_loc(bba, pin.location, "abs_loc");
        bba.u32(pin.bel_index, "bel_index");
        bba.s(pin.function, "function_name"); // Skip if empty?
        bba.u16(pin.bank, "bank");
        bba.u16(0, "padding");
	}

    bba.l("tiletype_names", "RelPtr<char>");
    for (auto& [tt, idx] : tiletype_names) {
        bba.s(tt, "name");
	}

	std::cout << "Writing speed grades..." << std::endl;
	
    for (std::string grade : speed_grade_names) {
        for (Cell cell : chips[grade].cells) {
            if (cell.delays.size() > 0) {
                bba.l("cell_" + std::to_string(cell.celltype) + "_delays_" + grade);
                for (Delay delay : cell.delays) {
                    bba.u32(delay.from_pin, "from_pin");
                    bba.u32(delay.to_pin, "to_pin");
                    bba.u32(delay.min_delay, "min_delay");
                    bba.u32(delay.max_delay, "max_delay");
				}
			}
			
            if (cell.setupholds.size() > 0) {
                bba.l("cell_" + std::to_string(cell.celltype) + "_setupholds_" + grade);
                for (SetupHold sh : cell.setupholds) {
                    bba.u32(sh.pin, "sig_port");
                    bba.u32(sh.clock, "clock_port");
                    bba.u32(sh.min_setup, "min_setup");
                    bba.u32(sh.max_setup, "max_setup");
                    bba.u32(sh.min_hold, "min_hold");
                    bba.u32(sh.max_hold, "max_hold");
				}
			}
		}
		
        bba.l("cell_timing_data_" + grade);
        for (Cell cell : chips[grade].cells) {
            bba.u32(cell.celltype, "cell_type");
            bba.u32(cell.delays.size(), "num_delays");
            bba.u32(cell.setupholds.size(), "num_setup_hold");
			if (cell.delays.size() > 0) {
				bba.r("cell_" + std::to_string(cell.celltype) + "_delays_" + grade, "delays");
			}
			else {
				bba.r("cell_" + std::to_string(cell.celltype) + "_delays_" + grade);
			}
			
			if (cell.delays.size() > 0) {
				bba.r("cell_" + std::to_string(cell.celltype) + "_setupholds_" + grade, "setupholds");
			}
			else {
				bba.r("cell_" + std::to_string(cell.celltype) + "_setupholds_" + grade);
			}
		}
		
        bba.l("pip_timing_data_" + grade);
        for (PipClass pipclass : chips[grade].pip_class_delays) {
            bba.u32(pipclass.min_delay, "min_delay");
            bba.u32(pipclass.max_delay, "max_delay");
            bba.u32(pipclass.min_fanout, "min_fanout");
            bba.u32(pipclass.max_fanout, "max_fanout");
		}
	}
	
	std::cout << "Writing speed grade data..." << std::endl;
			
    bba.l("speed_grade_data");
    for (std::string grade : speed_grade_names) {
		int delayCount = 0;
		for (Cell cell : chips[grade].cells) {
			delayCount += cell.delays.size();
		}
		
        bba.u32(delayCount * 3, "num_cell_timings"); // TODO: validate count.
        bba.u32(chips[grade].pip_class_delays.size(), "num_pip_classes");
        bba.r("cell_timing_data_" + grade, "cell_timings");
        bba.r("pip_timing_data_" + grade, "pip_classes");
	}
	
	std::cout << "Writing chip info..." << std::endl;

    bba.l("chip_info");
    bba.u32(max_col + 1, "width");
    bba.u32(max_row + 1, "height");
    bba.u32((max_col + 1) * (max_row + 1), "num_tiles");
    bba.u32(location_types.size(), "num_location_types");
    bba.u32(packages.size(), "num_packages");
    bba.u32(pindata.size(), "num_pios");

    bba.r("locations", "locations");
    bba.r("location_types", "location_type");
    bba.r("location_glbinfo", "location_glbinfo");
    bba.r("tiletype_names", "tiletype_names");
    bba.r("package_data", "package_info");
    bba.r("pio_info", "pio_info");
    bba.r("tiles_info", "tile_info");
    bba.r("speed_grade_data", "speed_grades");

    bba.pop();
	
	
}


int main(int argc, char** argv) {
	// -p or --constids option is followed by the path to the 'constids.inc' file that we must open.
	// the last string contains the name of the target device:
	// trellis_import.exe -p /path/to/constids.inc device
	
	Trellis::load_database("../../trellis/database");

    // Read port pin file
	if (argc != 4) {
		std::cerr << "Usage: trellis_import -p <constids.inc path> <device name>." << std::endl;
		return 1;
	}
	
	
	if (strncmp(argv[1], "-p", 2)) {
		std::cerr << "Invalid flag provided." << std::endl;
		return 1;
	}
	
	std::string constidsPath = argv[2];
	std::string device = argv[3];
	
	ifstream constidsFile(constidsPath);
	if (!constidsFile.is_open()) {
		std::cerr << "Failed to open constids file." << std::endl;
		return 1;
	}
	
	std::cout << "Parsing constids file..." << std::endl;
	
	std::string line, key;
	uint32_t index = 1;
	while (getline(constidsFile, line))	{
		if (line.length() < 4) {
			// Skip this line as it's likely empty.
			continue;
		}
		
		if (line.front() == 'X') {
			// Search for the first '(' character, read into 'key' string up till first ')'
			// character, if any. Otherwise skip the line (corrupted line?).
			size_t start = line.find_first_of('(');
			size_t end = line.find_first_of(')');
			if (start == std::string::npos || end == std::string::npos) {
				std::cerr << "Failed to parse line: " << line << std::endl;
				continue;
			}
			
			key = line.substr(start + 1, (end - start - 1));
			constids.insert(std::pair<std::string, uint32_t>(key, index++));
		}
	}    

    constids["SLICE"] = constids["TRELLIS_SLICE"];
    constids["PIO"] = constids["TRELLIS_IO"];

    // Initialising chip...
    Trellis::Chip chip = Trellis::Chip(dev_names[device]);
	
    // Building routing graph...
	std::cout << "Creating chip database instance..." << std::endl;
    std::shared_ptr<Trellis::DDChipDb::DedupChipdb> ddrg = Trellis::DDChipDb::make_dedup_chipdb(chip);
	std::cout << "Processing timing data..." << std::endl;
    process_timing_data();
	std::cout << "Processing PIO database..." << std::endl;
    process_pio_db(ddrg, device);
	std::cout << "Processing location globals." << std::endl;
    process_loc_globals(chip);
	
    // Writing database...
	std::cout << "Writing database to disk..." << std::endl;
    write_database(device, chip, ddrg, "le");
	
	std::cout << "Done." << std::endl;
	
	return 0;
}
