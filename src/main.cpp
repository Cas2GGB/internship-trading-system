#include <iostream>
#include "Engine.h"

int main(int argc, char** argv) {
    try {
        Engine engine;
        
        // Simple config path determination
        // Assumes running from bin/ directory or project root with correct relative path
        std::string configPath = "../conf/matching_engine.conf";
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
