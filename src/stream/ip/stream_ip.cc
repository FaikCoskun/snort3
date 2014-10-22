/****************************************************************************
 *
 * Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 * Copyright (C) 2005-2013 Sourcefire, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.  You may not use, modify or
 * distribute this program under any other version of the GNU General
 * Public License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 ****************************************************************************/

#include "stream_ip.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include "ip_module.h"
#include "ip_defrag.h"
#include "ip_session.h"
#include "log/messages.h"
#include "protocols/packet.h"

//-------------------------------------------------------------------------
// helpers
//-------------------------------------------------------------------------

StreamIpConfig::StreamIpConfig()
{
    session_timeout = 60;
}

static void ip_show (StreamIpConfig* pc)
{
    LogMessage("Stream IP config:\n");
    LogMessage("    Timeout: %d seconds\n", pc->session_timeout);
}

//-------------------------------------------------------------------------
// inspector stuff
//-------------------------------------------------------------------------

class StreamIp : public Inspector
{
public:
    StreamIp(StreamIpConfig*);
    ~StreamIp();

    bool configure(SnortConfig*) override;
    void show(SnortConfig*) override;

    void tinit() override;
    void tterm() override;

    void eval(Packet*) override;

public:
    StreamIpConfig* config;
    Defrag* defrag;
};

StreamIp::StreamIp (StreamIpConfig* c)
{
    config = c;
    defrag = new Defrag(c->frag_engine);
}

StreamIp::~StreamIp()
{
    delete defrag;
    delete config;
}

bool StreamIp::configure(SnortConfig* sc)
{
    defrag->configure(sc);
    return true;
}

void StreamIp::tinit()
{
    defrag->tinit();
}

void StreamIp::tterm()
{
    defrag->tterm();
}

void StreamIp::show(SnortConfig* sc)
{
    ip_show(config);
    defrag->show(sc);
}

void StreamIp::eval(Packet*)
{
    // session::process() instead
    assert(false);
}

StreamIpConfig* get_ip_cfg(Inspector* ins)
{
    assert(ins);
    return ((StreamIp*)ins)->config;
}

Defrag* get_defrag(Inspector* ins)
{
    assert(ins);
    return ((StreamIp*)ins)->defrag;
}

//-------------------------------------------------------------------------
// api stuff
//-------------------------------------------------------------------------

static Module* mod_ctor()
{ return new StreamIpModule; }

static void mod_dtor(Module* m)
{ delete m; }

static Inspector* ip_ctor(Module* m)
{
    StreamIpModule* mod = (StreamIpModule*)m;
    return new StreamIp(mod->get_data());
}

static void ip_dtor(Inspector* p)
{
    delete p;
}

static Session* ip_ssn(Flow* lws)
{
    return new IpSession(lws);
}

static const InspectApi ip_api =
{
    {
        PT_INSPECTOR,
        MOD_NAME,
        MOD_HELP,
        INSAPI_PLUGIN_V0,
        0,
        mod_ctor,
        mod_dtor
    },
    IT_STREAM,
    (unsigned)PktType::IP,
    nullptr, // buffers
    nullptr, // service
    nullptr, // pinit
    nullptr, // pterm
    nullptr, // tinit
    nullptr, // tterm
    ip_ctor,
    ip_dtor,
    ip_ssn,
    nullptr  // reset
};

const BaseApi* nin_stream_ip = &ip_api.base;
