/* Copyright (C) 2014 J.F.Dockes
 *	 This program is free software; you can redistribute it and/or modify
 *	 it under the terms of the GNU General Public License as published by
 *	 the Free Software Foundation; either version 2 of the License, or
 *	 (at your option) any later version.
 *
 *	 This program is distributed in the hope that it will be useful,
 *	 but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	 GNU General Public License for more details.
 *
 *	 You should have received a copy of the GNU General Public License
 *	 along with this program; if not, write to the
 *	 Free Software Foundation, Inc.,
 *	 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "ohreceiver.hxx"

#include <stdlib.h>                     // for atoi

#include <upnp/upnp.h>                  // for UPNP_E_SUCCESS, etc

#include <functional>                   // for _Bind, bind, _1, _2
#include <iostream>                     // for endl, etc
#include <string>                       // for string, allocator, etc
#include <utility>                      // for pair
#include <vector>                       // for vector

#define LOCAL_LOGINC 4
#include "libupnpp/log.hxx"             // for LOGDEB, LOGERR
#include "libupnpp/soaphelp.hxx"        // for SoapArgs, SoapData, i2s, etc

#include "mpdcli.hxx"                   // for MpdStatus, UpSong, MPDCli, etc
#include "upmpd.hxx"                    // for UpMpd, etc
#include "upmpdutils.hxx"               // for didlmake, diffmaps, etc
#include "ohplaylist.hxx"

using namespace std;
using namespace std::placeholders;

static const string sTpProduct("urn:av-openhome-org:service:Receiver:1");
static const string sIdProduct("urn:av-openhome-org:serviceId:Receiver");

OHReceiver::OHReceiver(UpMpd *dev, OHPlaylist *pl, int port)
    : UpnpService(sTpProduct, sIdProduct, dev), m_dev(dev), m_pl(pl), 
      m_httpport(port)
{
    dev->addActionMapping(this, "Play", 
                          bind(&OHReceiver::play, this, _1, _2));
    dev->addActionMapping(this, "Stop", 
                          bind(&OHReceiver::stop, this, _1, _2));
    dev->addActionMapping(this, "SetSender",
                          bind(&OHReceiver::setSender, this, _1, _2));
    dev->addActionMapping(this, "Sender", 
                          bind(&OHReceiver::sender, this, _1, _2));
    dev->addActionMapping(this, "ProtocolInfo",
                          bind(&OHReceiver::protocolInfo, this, _1, _2));
    dev->addActionMapping(this, "TransportState",
                          bind(&OHReceiver::transportState, this, _1, _2));

    m_httpuri = "http://localhost:"+ SoapHelp::i2s(m_httpport) + 
        "/Songcast.wav";
}

// Allowed states: Stopped, Playing,Waiting, Buffering
static string mpdsToTransportState(MpdStatus::State st)
{
    string tstate;

    switch(st) {
    case MpdStatus::MPDS_PLAY: 
        tstate = "Playing";
        break;
    default:
        tstate = "Stopped";
        break;
    }
    return tstate;
}

//                                 
static const string o_protocolinfo("ohz:*:*:*,ohm:*:*:*,ohu:*.*.*");
//static const string o_protocolinfo("ohu:*:*:*");

bool OHReceiver::makestate(unordered_map<string, string> &st)
{
    st.clear();

    const MpdStatus &mpds = m_dev->getMpdStatusNoUpdate();

    st["Uri"] = m_uri;
    st["Metadata"] = m_metadata;
    st["TransportState"] = mpdsToTransportState(mpds.state);
    st["ProtocolInfo"] = o_protocolinfo;
    return true;
}

bool OHReceiver::getEventData(bool all, std::vector<std::string>& names, 
                              std::vector<std::string>& values)
{
    //LOGDEB("OHReceiver::getEventData" << endl);

    unordered_map<string, string> state;

    makestate(state);

    unordered_map<string, string> changed;
    if (all) {
        changed = state;
    } else {
        changed = diffmaps(m_state, state);
    }
    m_state = state;

    for (auto it = changed.begin(); it != changed.end(); it++) {
        LOGDEB("OHReceiver::getEventData: changed: " << it->first <<
               " = " << it->second << endl);
        names.push_back(it->first);
        values.push_back(it->second);
    }

    return true;
}

void OHReceiver::maybeWakeUp(bool ok)
{
    if (ok && m_dev)
        m_dev->loopWakeup();
}

int OHReceiver::play(const SoapArgs& sc, SoapData& data)
{
    LOGDEB("OHReceiver::play" << endl);
    bool ok = false;

    if (!m_pl) {
        LOGERR("OHReceiver::play: no playlist service" << endl);
        return UPNP_E_INTERNAL_ERROR;
    }
    if (m_uri.empty()) {
        LOGERR("OHReceiver::play: no uri" << endl);
        return UPNP_E_INTERNAL_ERROR;
    }
    if (m_metadata.empty()) {
        LOGERR("OHReceiver::play: no metadata" << endl);
        return UPNP_E_INTERNAL_ERROR;
    }
        
    // We start the songcast command to receive the audio flux and
    // export it as HTTP, then insert http URI at the front of the
    // queue and execute next/play
    if (m_cmd)
        m_cmd->zapChild();
    m_cmd = shared_ptr<ExecCmd>(new ExecCmd());
    vector<string> args;
    args.push_back("-u");
    args.push_back(m_uri);
    LOGDEB("OHReceiver::play: executing scmpdcli" << endl);
    ok = m_cmd->startExec("scmpdcli", args, false, false) >= 0;
    if (!ok) {
        LOGERR("OHReceiver::play: executing scmpdcli failed" <<endl);
        goto out;
    } else {
        LOGDEB("OHReceiver::play: scmpdcli pid "<< m_cmd->getChildPid()<< endl);
    }

    // And insert the appropriate uri in the mpd playlist
    ok = m_pl->insertUri(0, m_httpuri, SoapHelp::xmlUnquote(m_metadata));
    if (!ok) {
        LOGERR("OHReceiver::play: insertUri() failed\n");
        goto out;
    }
    ok = m_dev->m_mpdcli->play(0);
    if (!ok) {
        LOGERR("OHReceiver::play: play() failed\n");
        goto out;
    }

    maybeWakeUp(ok);

out:
    return ok ? UPNP_E_SUCCESS : UPNP_E_INTERNAL_ERROR;
}

int OHReceiver::stop(const SoapArgs& sc, SoapData& data)
{
    LOGDEB("OHReceiver::stop" << endl);
    bool ok = false;
    m_cmd->zapChild();
    m_cmd = shared_ptr<ExecCmd>(0);
    ok = m_dev->m_mpdcli->stop();
    maybeWakeUp(ok);
    return ok ? UPNP_E_SUCCESS : UPNP_E_INTERNAL_ERROR;
}

int OHReceiver::setSender(const SoapArgs& sc, SoapData& data)
{
    LOGDEB("OHReceiver::setSender" << endl);
    string uri, metadata;
    bool ok = sc.getString("Uri", &uri) &&
        sc.getString("Metadata", &metadata);

    if (ok) {
        m_uri = uri;
        m_metadata = metadata;
        LOGDEB("OHReceiver::setSender: uri [" << m_uri << "] meta [" << 
               m_metadata << "]" << endl);
    }

    maybeWakeUp(ok);
    return ok ? UPNP_E_SUCCESS : UPNP_E_INTERNAL_ERROR;
}

int OHReceiver::sender(const SoapArgs& sc, SoapData& data)
{
    LOGDEB("OHReceiver::sender" << endl);
    data.addarg("Uri", m_uri);
    data.addarg("Metadata", m_metadata);
    return UPNP_E_SUCCESS;
}

int OHReceiver::transportState(const SoapArgs& sc, SoapData& data)
{
    //LOGDEB("OHReceiver::transportState" << endl);
    const MpdStatus &mpds = m_dev->getMpdStatusNoUpdate();
    string tstate = mpdsToTransportState(mpds.state);
    data.addarg("Value", tstate);
    LOGDEB("OHReceiver::transportState: " << tstate << endl);
    return UPNP_E_SUCCESS;
}

int OHReceiver::protocolInfo(const SoapArgs& sc, SoapData& data)
{
    LOGDEB("OHReceiver::protocolInfo" << endl);
    data.addarg("Value", o_protocolinfo);
    return UPNP_E_SUCCESS;
}
