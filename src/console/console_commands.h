#pragma once
#include <string>
#include "config/config.h"
#include "mapper/mapper.h"
#include "output/synthetic_input.h"

namespace windeck::console {

class ConsoleCommands {
public:
    ConsoleCommands(windeck::config::Config& config, windeck::mapper::Mapper& mapper,
                    windeck::output::SyntheticInput& synth);
    void process(const std::string& line);

private:
    windeck::config::Config&  config_;
    windeck::mapper::Mapper&  mapper_;
    windeck::output::SyntheticInput& synth_;
};

} // namespace windeck::console
