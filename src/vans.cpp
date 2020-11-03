#include "config.h"
#include "general/factory.h"
#include "general/trace.h"
#include <iostream>
#include <string>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[])
{
    string trace_filename;
    string config_filename;

    int c;
    while (-1 != (c = getopt(argc, argv, "c:t:"))) {
        switch (c) {
        case 'c':
            config_filename = optarg;
            break;
        case 't':
            trace_filename = optarg;
            break;
        default:
            cout << "Usage: "
                 << "-c cfg_filename -t trace_filename" << endl;
            return 0;
        }
    }

    auto cfg   = vans::root_config(config_filename);
    auto model = vans::factory::make(cfg);
    vans::trace::run_trace(cfg, trace_filename, model);

    return 0;
}
