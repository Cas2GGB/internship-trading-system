#include <iostream>
#include <fstream>
#include "Engine.h"

int main(int argc, char** argv) {
    try {
        Engine engine;
        
        // Check for config at different locations
        std::string configPath = "conf/matching_engine.conf";
        {
            std::ifstream f(configPath);
            if (!f.good()) {
                configPath = "../conf/matching_engine.conf";
            }
        }
        
        engine.init(configPath);
        
        if (argc > 1) {
            std::cout << "Loading script: " << argv[1] << std::endl;
            engine.loadScript(argv[1]);
        }
        
        engine.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
