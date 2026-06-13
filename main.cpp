#include "engine/engine.hpp"

int main(int argc, char* argv[]) {
    quantumflow::Config cfg = quantumflow::parse_args(argc, argv);

    quantumflow::Engine engine(std::move(cfg));
    if (!engine.init()) {
        return 1;
    }
    engine.run();
    return 0;
}
