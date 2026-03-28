#pragma once
#include <string>
#include "config/config.h"
#include "mapper/mapper.h"

namespace windeck::console {

class ConsoleCommands {
public:
    ConsoleCommands(windeck::config::Config& config, windeck::mapper::Mapper& mapper);
    void process(const std::string& line);

private:
    windeck::config::Config&  config_;
    windeck::mapper::Mapper&  mapper_;
};

} // namespace windeck::console
