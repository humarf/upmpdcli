/* Copyright (C) 2014 J.F.Dockes
 *       This program is free software; you can redistribute it and/or modify
 *       it under the terms of the GNU General Public License as published by
 *       the Free Software Foundation; either version 2 of the License, or
 *       (at your option) any later version.
 *
 *       This program is distributed in the hope that it will be useful,
 *       but WITHOUT ANY WARRANTY; without even the implied warranty of
 *       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *       GNU General Public License for more details.
 *
 *       You should have received a copy of the GNU General Public License
 *       along with this program; if not, write to the
 *       Free Software Foundation, Inc.,
 *       59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <string>
#include <functional>

using namespace std;
using namespace std::placeholders;

#include <upnp/upnp.h>

#include "libupnpp/soaphelp.hxx"
#include "libupnpp/upnpp_p.hxx"
#include "libupnpp/log.hxx"
#include "libupnpp/upnpavutils.hxx"
#include "libupnpp/control/avtransport.hxx"
#include "libupnpp/control/avlastchg.hxx"
#include "libupnpp/control/cdircontent.hxx"

namespace UPnPClient {

const string AVTransport::SType("urn:schemas-upnp-org:service:AVTransport:1");

// Check serviceType string (while walking the descriptions. We don't
// include a version in comparisons, as we are satisfied with version1
bool AVTransport::isAVTService(const string& st)
{
    const string::size_type sz(SType.size()-2);
    return !SType.compare(0, sz, st, 0, sz);
}

static AVTransport::TransportState stringToTpState(const string& s)
{
    if (!stringuppercmp("STOPPED", s)) {
        return AVTransport::Stopped;
    } else if (!stringuppercmp("PLAYING", s)) {
        return AVTransport::Playing;
    } else if (!stringuppercmp("TRANSITIONING", s)) {
        return AVTransport::Transitioning;
    } else if (!stringuppercmp("PAUSED_PLAYBACK", s)) {
        return AVTransport::PausedPlayback;
    } else if (!stringuppercmp("PAUSED_RECORDING", s)) {
        return AVTransport::PausedRecording;
    } else if (!stringuppercmp("RECORDING", s)) {
        return AVTransport::Recording;
    } else if (!stringuppercmp("NO MEDIA PRESENT", s)) {
        return AVTransport::NoMediaPresent;
    } else {
        LOGERR("AVTransport event: bad value for TransportState: " 
               << s << endl);
        return AVTransport::Unknown;
    }
}

static AVTransport::TransportStatus stringToTpStatus(const string& s)
{
    if (!stringuppercmp("OK", s)) {
        return AVTransport::TPS_Ok;
    } else if (!stringuppercmp("ERROR_OCCURRED", s)) {
        return  AVTransport::TPS_Error;
    } else {
        LOGERR("AVTransport event: bad value for TransportStatus: " 
               << s << endl);
        return  AVTransport::TPS_Unknown;
    }
}

static AVTransport::PlayMode stringToPlayMode(const string& s)
{
    if (!stringuppercmp("NORMAL", s)) {
        return AVTransport::PM_Normal;
    } else if (!stringuppercmp("SHUFFLE", s)) {
        return AVTransport::PM_Shuffle;
    } else if (!stringuppercmp("REPEAT_ONE", s)) {
        return AVTransport::PM_RepeatOne;
    } else if (!stringuppercmp("REPEAT_ALL", s)) {
        return AVTransport::PM_RepeatAll;
    } else if (!stringuppercmp("RANDOM", s)) {
        return AVTransport::PM_Random;
    } else if (!stringuppercmp("DIRECT_1", s)) {
        return AVTransport::PM_Direct1;
    } else {
        LOGERR("AVTransport event: bad value for PlayMode: " 
               << s << endl);
        return AVTransport::PM_Unknown;
    }
}

void AVTransport::evtCallback(
    const std::unordered_map<std::string, std::string>& props)
{
    LOGDEB1("AVTransport::evtCallback:" << endl);
    for (auto& entry: props) {
        if (entry.first.compare("LastChange")) {
            LOGINF("AVTransport:event: var not lastchange: "
                   << entry.first << " -> " << entry.second << endl;);
            continue;
        }
        LOGDEB1("AVTransport:event: "
                << entry.first << " -> " << entry.second << endl;);

        std::unordered_map<std::string, std::string> props1;
        if (!decodeAVLastChange(entry.second, props1)) {
            LOGERR("AVTransport::evtCallback: bad LastChange value: "
                   << entry.second << endl);
            return;
        }
        for (auto& entry1: props1) {
            if (!m_reporter) {
                LOGDEB1("AVTransport::evtCallback: " << entry1.first << " -> " 
                       << entry1.second << endl);
                continue;
            }

            if (!entry1.first.compare("TransportState")) {
                m_reporter->changed(entry1.first.c_str(), 
                                    stringToTpState(entry1.second));

            } else if (!entry1.first.compare("TransportStatus")) {
                m_reporter->changed(entry1.first.c_str(), 
                                    stringToTpStatus(entry1.second));

            } else if (!entry1.first.compare("CurrentPlayMode")) {
                m_reporter->changed(entry1.first.c_str(), 
                                    stringToPlayMode(entry1.second));

            } else if (!entry1.first.compare("CurrentTransportActions") ||
                       !entry1.first.compare("CurrentTrackURI") ||
                       !entry1.first.compare("AVTransportURI") ||
                       !entry1.first.compare("NextAVTransportURI")) {
                m_reporter->changed(entry1.first.c_str(), 
                                    entry1.second.c_str());

            } else if (!entry1.first.compare("TransportPlaySpeed") ||
                       !entry1.first.compare("CurrentTrack") ||
                       !entry1.first.compare("NumberOfTracks") ||
                       !entry1.first.compare("RelativeCounterPosition") ||
                       !entry1.first.compare("AbsoluteCounterPosition") ||
                       !entry1.first.compare("InstanceID")) {
                m_reporter->changed(entry1.first.c_str(),
                                    atoi(entry1.second.c_str()));

            } else if (!entry1.first.compare("CurrentMediaDuration") ||
                       !entry1.first.compare("CurrentTrackDuration") ||
                       !entry1.first.compare("RelativeTimePosition") ||
                       !entry1.first.compare("AbsoluteTimePosition")) {
                m_reporter->changed(entry1.first.c_str(),
                                    upnpdurationtos(entry1.second));

            } else if (!entry1.first.compare("AVTransportURIMetaData") ||
                       !entry1.first.compare("NextAVTransportURIMetaData") ||
                       !entry1.first.compare("CurrentTrackMetaData")) {
                UPnPDirContent meta;
                if (!meta.parse(entry1.second)) {
                    LOGERR("AVTransport event: bad metadata: [" <<
                           entry1.second << "]" << endl);
                } else {
                    LOGDEB1("AVTransport event: metadata: " << 
                           meta.m_items.size() << " items " << endl);
                }
                
                m_reporter->changed(entry1.first.c_str(), meta);

            } else if (!entry1.first.compare("PlaybackStorageMedium") ||
                       !entry1.first.compare("PossiblePlaybackStorageMedium") ||
                       !entry1.first.compare("RecordStorageMedium") ||
                       !entry1.first.compare("PossibleRecordStorageMedium") ||
                       !entry1.first.compare("RecordMediumWriteStatus") ||
                       !entry1.first.compare("CurrentRecordQualityMode") ||
                       !entry1.first.compare("PossibleRecordQualityModes")){
                m_reporter->changed(entry1.first.c_str(),entry1.second.c_str());

            } else {
                LOGERR("AVTransport event: unknown variable: name [" <<
                       entry1.first << "] value [" << entry1.second << endl);
                m_reporter->changed(entry1.first.c_str(),entry1.second.c_str());
            }
        }
    }
}


int AVTransport::setURI(const string& uri, const string& metadata,
                        int instanceID, bool next)
{
    SoapEncodeInput args(m_serviceType, next ? "SetNextAVTransportURI" :
                         "SetAVTransportURI");
    args("InstanceID", SoapHelp::i2s(instanceID))
        (next ? "NextURI" : "CurrentURI", uri)
        (next ? "NextURIMetaData" : "CurrentURIMetaData", metadata);

    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::setPlayMode(PlayMode pm, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "SetPlayMode");
    string playmode;
    switch (pm) {
    case PM_Normal: playmode = "NORMAL"; break;
    case PM_Shuffle: playmode = "SHUFFLE"; break;
    case PM_RepeatOne: playmode = "REPEAT_ONE"; break;
    case PM_RepeatAll: playmode = "REPEAT_ALL"; break;
    case PM_Random: playmode = "RANDOM"; break;
    case PM_Direct1: playmode = "DIRECT_1"; break;
    default: playmode = "NORMAL"; break;
    }

    args("InstanceID", SoapHelp::i2s(instanceID))
        ("NewPlayMode", playmode);

    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::getMediaInfo(MediaInfo& info, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "GetMediaInfo");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    string s;
    data.getInt("NrTracks", &info.nrtracks);
    data.getString("MediaDuration", &s);
    info.mduration = upnpdurationtos(s);
    data.getString("CurrentURI", &info.cururi);
    data.getString("CurrentURIMetaData", &s);
    UPnPDirContent meta;
    meta.parse(s);
    if (meta.m_items.size() > 0)
        info.curmeta = meta.m_items[0];
    meta.clear();
    data.getString("NextURI", &info.nexturi);
    data.getString("NextURIMetaData", &s);
    if (meta.m_items.size() > 0)
        info.nextmeta = meta.m_items[0];
    data.getString("PlayMedium", &info.pbstoragemed);
    data.getString("RecordMedium", &info.pbstoragemed);
    data.getString("WriteStatus", &info.ws);
    return 0;
}

int AVTransport::getTransportInfo(TransportInfo& info, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "GetTransportInfo");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    string s;
    data.getString("CurrentTransportState", &s);
    info.tpstate = stringToTpState(s);
    data.getString("CurrentTransportStatus", &s);
    info.tpstatus = stringToTpStatus(s);
    data.getInt("CurrentSpeed", &info.curspeed);
    return 0;
}

int AVTransport::getPositionInfo(PositionInfo& info, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "GetPositionInfo");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    string s;
    data.getInt("Track", &info.track);
    data.getString("TrackDuration", &s);
    info.trackduration = upnpdurationtos(s);
    data.getString("TrackMetaData", &s);
    UPnPDirContent meta;
    meta.parse(s);
    if (meta.m_items.size() > 0) {
        info.trackmeta = meta.m_items[0];
            LOGDEB("AVTransport::getPositionInfo: size " << 
           meta.m_items.size() << " current title: " << meta.m_items[0].m_title
           << endl);
    }
    meta.clear();
    data.getString("TrackURI", &info.trackuri);
    data.getString("RelTime", &s);
    info.reltime = upnpdurationtos(s);
    data.getString("AbsTime", &s);
    info.abstime = upnpdurationtos(s);
    data.getInt("RelCount", &info.relcount);
    data.getInt("AbsCount", &info.abscount);
    return 0;
}

int AVTransport::getDeviceCapabilities(DeviceCapabilities& info, int iID)
{
    SoapEncodeInput args(m_serviceType, "GetDeviceCapabilities");
    args("InstanceID", SoapHelp::i2s(iID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    data.getString("PlayMedia", &info.playmedia);
    data.getString("RecMedia", &info.recmedia);
    data.getString("RecQualityModes", &info.recqualitymodes);
    return 0;
}

int AVTransport::getTransportSettings(TransportSettings& info, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "GetTransportSettings");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    string s;
    data.getString("PlayMedia", &s);
    info.playmode = stringToPlayMode(s);
    data.getString("RecQualityMode", &info.recqualitymode);
    return 0;
}

int AVTransport::getCurrentTransportActions(std::string& actions, int iID)
{
    SoapEncodeInput args(m_serviceType, "GetCurrentTransportActions");
    args("InstanceID", SoapHelp::i2s(iID));
    SoapDecodeOutput data;
    int ret = runAction(args, data);
    if (ret != UPNP_E_SUCCESS) {
        return ret;
    }
    data.getString("Actions", &actions);
    return 0;
}

int AVTransport::stop(int instanceID)
{
    SoapEncodeInput args(m_serviceType, "Stop");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::pause(int instanceID)
{
    SoapEncodeInput args(m_serviceType, "Pause");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::play(int speed, int instanceID)
{
    SoapEncodeInput args(m_serviceType, "Play");
    args("InstanceID", SoapHelp::i2s(instanceID))
        ("Speed", SoapHelp::i2s(speed));
    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::seek(SeekMode mode, int target, int instanceID)
{
    string sm;
    switch (mode) {
    case SEEK_TRACK_NR: sm = "TRACK_NR"; break;
    case SEEK_ABS_TIME: sm = "ABS_TIME"; break;
    case SEEK_REL_TIME: sm = "REL_TIME"; break;
    case SEEK_ABS_COUNT: sm = "ABS_COUNT"; break;
    case SEEK_REL_COUNT: sm = "REL_COUNT"; break;
    case SEEK_CHANNEL_FREQ: sm = "CHANNEL_FREQ"; break;
    case SEEK_TAPE_INDEX: sm = "TAPE-INDEX"; break;
    case SEEK_FRAME: sm = "FRAME"; break;
    default:
        return UPNP_E_INVALID_PARAM;
    }

    SoapEncodeInput args(m_serviceType, "Play");
    args("InstanceID", SoapHelp::i2s(instanceID))
        ("Unit", sm)
        ("Target", SoapHelp::i2s(target));
    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::next(int instanceID)
{
    SoapEncodeInput args(m_serviceType, "Next");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    return runAction(args, data);
}

int AVTransport::previous(int instanceID)
{
    SoapEncodeInput args(m_serviceType, "Previous");
    args("InstanceID", SoapHelp::i2s(instanceID));
    SoapDecodeOutput data;
    return runAction(args, data);
}

void AVTransport::registerCallback()
{
    Service::registerCallback(bind(&AVTransport::evtCallback, this, _1));
}

} // End namespace UPnPClient
