// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringView>
#include <QString>
#include <optional>

namespace dsdr::core {
struct AprsPosition { double latitude, longitude; QString objectName; };
// Parse compressed or unambiguous uncompressed APRS position reports.
// Position-ambiguity spaces and other APRS packet types are not inferred.
inline std::optional<AprsPosition> parseAprsPosition(QStringView info) {
    if(info.isEmpty()) return {};
    const QChar kind=info[0];
    int offset=1, timestampStart=0, itemEnd=0;
    if(kind==u'/' || kind==u'@') offset=8;
    else if(kind==u';') {
        if(info.size()<18 || info[10]!=u'*') return {};
        offset=18; timestampStart=11;
    }
    else if(kind==u')') {
        for(int i=1;i<info.size() && i<=10;++i) {
            if(info[i]==u'!' || info[i]==u'_') { itemEnd=i; break; }
        }
        if(itemEnd<4 || itemEnd>10 || info[itemEnd]!=u'!') return {};
        offset=itemEnd+1;
    }
    else if(kind!=u'!' && kind!=u'=') return {};
    if(kind==u'/' || kind==u'@') timestampStart=1;
    auto digit=[](QChar c) { return c>=u'0' && c<=u'9'; };
    QString objectName;
    if(kind==u';') {
        for(int i=1;i<=9;++i) if(info[i]<u' ' || info[i]>u'~') return {};
        objectName=info.mid(1,9).toString().trimmed();
        if(objectName.isEmpty()) return {};
    } else if(kind==u')') {
        for(int i=1;i<itemEnd;++i) if(info[i]<u' ' || info[i]>u'~') return {};
        objectName=info.mid(1,itemEnd-1).toString();
    }
    if(timestampStart) {
        if(info.size()<timestampStart+7) return {};
        for(int i=timestampStart;i<timestampStart+6;++i) if(!digit(info[i])) return {};
        const QChar zone=info[timestampStart+6];
        if(zone!=u'z' && zone!=u'h' && zone!=u'/') return {};
        const auto pair=[&](int i) { return (info[i].unicode()-u'0')*10+info[i+1].unicode()-u'0'; };
        if(zone==u'h') {
            if(pair(timestampStart)>23 || pair(timestampStart+2)>59 || pair(timestampStart+4)>59) return {};
        } else if(pair(timestampStart)<1 || pair(timestampStart)>31 || pair(timestampStart+2)>23 || pair(timestampStart+4)>59) return {};
    }
    if(info.size()<offset+1) return {};
    const QChar table=info[offset];
    if(table==u'/' || table==u'\\' || (table>=u'A' && table<=u'Z') || (table>=u'a' && table<=u'j')) {
        if(info.size()<offset+13) return {};
        unsigned y=0, x=0;
        for(int i=1;i<=8;++i) {
            const auto c=info[offset+i].unicode();
            if(c<33 || c>123) return {};
            if(i<=4) y=y*91+(c-33); else x=x*91+(c-33);
        }
        if(info[offset+9]<u'!' || info[offset+9]>u'~') return {};
        if(info[offset+10]!=u' ') {
            for(int i=10;i<=12;++i) if(info[offset+i]<u'!' || info[offset+i]>u'{') return {};
        }
        const double latitude=90.0-y/380926.0, longitude=-180.0+x/190463.0;
        if(latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) return {};
        return AprsPosition{latitude,longitude,objectName};
    }
    if(info.size()<offset+19) return {};
    const auto lat=info.mid(offset,8), lon=info.mid(offset+9,9);
    const QChar uncompressedTable=info[offset+8];
    if(!(uncompressedTable==u'/' || uncompressedTable==u'\\' ||
         (uncompressedTable>=u'0' && uncompressedTable<=u'9') ||
         (uncompressedTable>=u'A' && uncompressedTable<=u'Z')) ||
       info[offset+18]<u'!' || info[offset+18]>u'~') return {};
    for(int i:{0,1,2,3,5,6}) if(!digit(lat[i])) return {};
    for(int i:{0,1,2,3,4,6,7}) if(!digit(lon[i])) return {};
    if(lat[4]!=u'.' || lon[5]!=u'.' || (lat[7]!=u'N' && lat[7]!=u'S') ||
       (lon[8]!=u'E' && lon[8]!=u'W')) return {};
    auto number=[](QChar c) { return c.unicode()-u'0'; };
    const int latDeg=number(lat[0])*10+number(lat[1]);
    const int lonDeg=number(lon[0])*100+number(lon[1])*10+number(lon[2]);
    const int latMin=number(lat[2])*1000+number(lat[3])*100+number(lat[5])*10+number(lat[6]);
    const int lonMin=number(lon[3])*1000+number(lon[4])*100+number(lon[6])*10+number(lon[7]);
    if(latMin>=6000 || lonMin>=6000 || latDeg>90 || lonDeg>180 ||
       (latDeg==90 && latMin) || (lonDeg==180 && lonMin)) return {};
    double y=latDeg+latMin/6000.0, x=lonDeg+lonMin/6000.0;
    if(lat[7]==u'S') y=-y; if(lon[8]==u'W') x=-x;
    return AprsPosition{y,x,objectName};
}
}
