//--------------------------------------------------------------------------
// Copyright (C) 2016-2019 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// sfdaq_module.cc author Michael Altizer <mialtize@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sfdaq_module.h"

#include <cassert>

#include "log/messages.h"
#include "main/snort_config.h"

#include "active.h"
#include "sfdaq.h"
#include "sfdaq_config.h"
#include "trough.h"

using namespace snort;

#define sfdaq_help "configure packet acquisition interface"

/*
 * Module Configuration
 */

static const Parameter daqvar_list_param[] =
{
    { "variable", Parameter::PT_STRING, nullptr, nullptr, "DAQ module variable (foo[=bar])" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

static const Parameter daq_module_param[] = 
{
    { "name", Parameter::PT_STRING, nullptr, nullptr, "DAQ module name (required)" },
    { "mode", Parameter::PT_ENUM, "passive | inline | read-file", "passive", "DAQ module mode" },
    { "variables", Parameter::PT_LIST, daqvar_list_param, nullptr, "DAQ module variables" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

static const Parameter path_list_param[] =
{
    { "path", Parameter::PT_STRING, nullptr, nullptr, "directory path" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

static const Parameter input_list_param[] =
{
    { "input", Parameter::PT_STRING, nullptr, nullptr, "input source" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

static const Parameter s_params[] =
{
    { "module_dirs", Parameter::PT_LIST, path_list_param, nullptr, "directories to search for dynamic DAQ modules" },
    { "inputs", Parameter::PT_LIST, input_list_param, nullptr, "input sources" },
    { "snaplen", Parameter::PT_INT, "0:65535", "1518", "set snap length (same as -s)" },
    { "batch_size", Parameter::PT_INT, "1:", "64", "set receive batch size (same as --daq-batch-size)" },
    { "modules", Parameter::PT_LIST, daq_module_param, nullptr, "DAQ modules to use" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

SFDAQModule::SFDAQModule() : Module("daq", sfdaq_help, s_params)
{
    config = nullptr;
    module_config = nullptr;
}

bool SFDAQModule::begin(const char* fqn, int idx, SnortConfig*)
{
    if (!strcmp(fqn, "daq"))
        config = new SFDAQConfig();
    else if (!strcmp(fqn, "daq.modules"))
    {
        if (idx == 0)
            return true;

        module_config = new SFDAQModuleConfig();
    }

    return true;
}

bool SFDAQModule::set(const char* fqn, Value& v, SnortConfig*)
{
    if (!strcmp(fqn, "daq.module_dirs"))
    {
        config->add_module_dir(v.get_string());
    }
    else if (!strcmp(fqn, "daq.inputs"))
    {
        config->add_input(v.get_string());
    }
    else if (!strcmp(fqn, "daq.snaplen"))
    {
        config->set_mru_size(v.get_uint16());
    }
    else if (!strcmp(fqn, "daq.batch_size"))
    {
        config->set_batch_size(v.get_long());
    }
    else if (!strcmp(fqn, "daq.modules.name"))
    {
        module_config->name = v.get_string();
    }
    else if (!strcmp(fqn, "daq.modules.mode"))
    {
        module_config->mode = (SFDAQModuleConfig::SFDAQMode) (v.get_long() + 1);
    }
    else if (!strcmp(fqn, "daq.modules.variables"))
    {
        module_config->set_variable(v.get_string());
    }

    return true;
}

bool SFDAQModule::end(const char* fqn, int idx, SnortConfig* sc)
{
    if (!strcmp(fqn, "daq.modules"))
    {
        if (idx == 0)
            return true;

        if (module_config->name.empty())
        {
            ParseError("%s - No module name specified!", fqn);
            delete module_config;
            module_config = nullptr;
            return false;
        }
        config->module_configs.push_back(module_config);
        module_config = nullptr;
    }
    else if (!strcmp(fqn, "daq"))
    {
        if ( sc->daq_config )
            delete sc->daq_config;

        sc->daq_config = config;
        config = nullptr;
    }

    return true;
}

/*
 * Module Counters
 */

struct DAQStats
{
    PegCount pcaps;
    PegCount received;
    PegCount analyzed;
    PegCount dropped;
    PegCount filtered;
    PegCount outstanding;
    PegCount injected;
    PegCount verdicts[MAX_DAQ_VERDICT];
    PegCount internal_blacklist;
    PegCount internal_whitelist;
    PegCount skipped;
    PegCount idle;
    PegCount rx_bytes;
};

const PegInfo daq_names[] =
{
    { CountType::MAX, "pcaps", "total files and interfaces processed" },
    { CountType::SUM, "received", "total packets received from DAQ" },
    { CountType::SUM, "analyzed", "total packets analyzed from DAQ" },
    { CountType::SUM, "dropped", "packets dropped" },
    { CountType::SUM, "filtered", "packets filtered out" },
    { CountType::SUM, "outstanding", "packets unprocessed" },
    { CountType::SUM, "injected", "active responses or replacements" },
    { CountType::SUM, "allow", "total allow verdicts" },
    { CountType::SUM, "block", "total block verdicts" },
    { CountType::SUM, "replace", "total replace verdicts" },
    { CountType::SUM, "whitelist", "total whitelist verdicts" },
    { CountType::SUM, "blacklist", "total blacklist verdicts" },
    { CountType::SUM, "ignore", "total ignore verdicts" },
    { CountType::SUM, "retry", "total retry verdicts" },

    // FIXIT-L these are not exactly DAQ counts - but they are related
    { CountType::SUM, "internal_blacklist",
        "packets blacklisted internally due to lack of DAQ support" },
    { CountType::SUM, "internal_whitelist",
        "packets whitelisted internally due to lack of DAQ support" },
    { CountType::SUM, "skipped", "packets skipped at startup" },
    { CountType::SUM, "idle", "attempts to acquire from DAQ without available packets" },
    { CountType::SUM, "rx_bytes", "total bytes received" },
    { CountType::END, nullptr, nullptr }
};

static THREAD_LOCAL DAQStats stats;

const PegInfo* SFDAQModule::get_pegs() const
{
    return daq_names;
}

PegCount* SFDAQModule::get_counts() const
{
    return (PegCount*) &stats;
}

static DAQ_Stats_t operator-(const DAQ_Stats_t& left, const DAQ_Stats_t& right)
{
    DAQ_Stats_t ret;

    ret.hw_packets_received = left.hw_packets_received - right.hw_packets_received;
    ret.hw_packets_dropped = left.hw_packets_dropped - right.hw_packets_dropped;
    ret.packets_received = left.packets_received - right.packets_received;
    ret.packets_filtered = left.packets_filtered - right.packets_filtered;
    ret.packets_injected = left.packets_injected - right.packets_injected;

    for ( unsigned i = 0; i < MAX_DAQ_VERDICT; i++ )
        ret.verdicts[i] = left.verdicts[i] - right.verdicts[i];

    return ret;
}

void SFDAQModule::prep_counts()
{
    static THREAD_LOCAL DAQ_Stats_t sfdaq_stats;
    static THREAD_LOCAL bool did_init = false;

    if ( !did_init )
    {
        memset(&sfdaq_stats, 0, sizeof(DAQ_Stats_t));
        did_init = true;
    }

    if ( SFDAQ::get_local_instance() == nullptr )
        return;

    DAQ_Stats_t new_sfdaq_stats = *SFDAQ::get_stats();

    // must subtract explicitly; can't zero; daq stats are cumulative ...
    DAQ_Stats_t sfdaq_stats_delta = new_sfdaq_stats - sfdaq_stats;

    uint64_t pkts_out = new_sfdaq_stats.hw_packets_received -
                        new_sfdaq_stats.packets_filtered -
                        new_sfdaq_stats.packets_received;

    stats.pcaps = Trough::get_file_count();
    stats.received = sfdaq_stats_delta.hw_packets_received;
    stats.analyzed = sfdaq_stats_delta.packets_received;
    stats.dropped = sfdaq_stats_delta.hw_packets_dropped;
    stats.filtered =  sfdaq_stats_delta.packets_filtered;
    stats.outstanding =  pkts_out;
    stats.injected =  sfdaq_stats_delta.packets_injected;

    for ( unsigned i = 0; i < MAX_DAQ_VERDICT; i++ )
        stats.verdicts[i] = sfdaq_stats_delta.verdicts[i];

    stats.internal_blacklist = aux_counts.internal_blacklist;
    stats.internal_whitelist = aux_counts.internal_whitelist;
    stats.skipped = aux_counts.skipped;
    stats.idle = aux_counts.idle;
    stats.rx_bytes = aux_counts.rx_bytes;

    memset(&aux_counts, 0, sizeof(AuxCount));

    sfdaq_stats = new_sfdaq_stats;
    for ( unsigned i = 0; i < MAX_DAQ_VERDICT; i++ )
        sfdaq_stats.verdicts[i] = new_sfdaq_stats.verdicts[i];
}

