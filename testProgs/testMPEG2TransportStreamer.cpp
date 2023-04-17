/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2023, Live Networks, Inc.  All rights reserved
// A test program that reads a MPEG-2 Transport Stream file,
// and streams it using RTP
// main program

#include <cstring>
#include <string>
#include <iostream>
#include <algorithm>
#include <string.h>
#include <vector>

#include "liveMedia.hh"

#include "BasicUsageEnvironment.hh"
#include "announceURL.hh"
#include "GroupsockHelper.hh"

// To stream using "source-specific multicast" (SSM), uncomment the following:
//#define USE_SSM 1

// To stream using IPv6 multicast, rather than IPv4 multicast, uncomment the following:
//#define USE_IPV6_MULTICAST 1

// To set up an internal RTSP server, uncomment the following:
//#define IMPLEMENT_RTSP_SERVER 1
// (Note that this RTSP server works for multicast only)

#ifdef USE_SSM
Boolean const isSSM = True;
#else
Boolean const isSSM = False;
#endif

#define TRANSPORT_PACKET_SIZE 188
#define TRANSPORT_PACKETS_PER_NETWORK_PACKET 7
// The product of these two numbers must be enough to fit within a network packet

UsageEnvironment* env;
// char const* inputFileName = "test.ts";
char *inputFileName;
char *destinationAddressStr;
unsigned short rtpPortNum;
long count = 1;
FramedSource* videoSource;
RTPSink* videoSink;

void play(); // forward

#define DEBUG 0
#if DEBUG == 1
#define LOG_D(fmt, ...) printf("%s[%d]: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#else
#define LOG_D(fmt, ...)
#endif
#define LOG_I(fmt, ...) printf(fmt, ##__VA_ARGS__);
#define LOG_E(fmt, ...) printf("\e[1;31m" fmt "\e[0m", ##__VA_ARGS__)

class InputParser{
    public:
        InputParser (int &argc, char **argv){
            for (int i=1; i < argc; ++i)
                this->tokens.push_back(std::string(argv[i]));
        }
        /// @author iain
        const std::string& getCmdOption(const std::string &option) const{
            std::vector<std::string>::const_iterator itr;
            itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
            if (itr != this->tokens.end() && ++itr != this->tokens.end()){
                return *itr;
            }
            static const std::string empty_string("");
            return empty_string;
        }
        /// @author iain
        bool cmdOptionExists(const std::string &option) const{
            return std::find(this->tokens.begin(), this->tokens.end(), option)
                != this->tokens.end();
        }
    private:
        std::vector <std::string> tokens;
};

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);
  InputParser parse_arg(argc, argv);
  if(parse_arg.cmdOptionExists("-h")) {
    LOG_I("Usage: %s -[f|m|p|h] [VALUES]\n", argv[0]);
    LOG_I("       -f file name\n");
    LOG_I("       -m multicast address\n");
    LOG_I("       -p port\n");
    LOG_I("       -h this help message\n");
    exit(0);
  }
  if(parse_arg.cmdOptionExists("-f")) {
    inputFileName = (char *) malloc(strlen( parse_arg.getCmdOption("-f").c_str() ) + 1 );
    // strcpy(inputFileName, argv[1]);
    strcpy(inputFileName, parse_arg.getCmdOption("-f").c_str());
    LOG_D("inputFileName: \'%s\'\n", inputFileName);
  } else {
    LOG_E("missing option: -f\n");
    LOG_E("Try \'%s -h\' for information/usage\n", argv[0]);
  }
  if(parse_arg.cmdOptionExists("-m")) {
    destinationAddressStr = (char *) malloc( strlen( parse_arg.getCmdOption("-m").c_str() ) + 1 );
    strcpy(destinationAddressStr, parse_arg.getCmdOption("-m").c_str());
    LOG_D("multicast address: %s\n", destinationAddressStr);
  } else {
    LOG_E("missing option: -m\n");
    LOG_E("Try \'%s -h\' for information/usage\n", argv[0]);
  }
  if(parse_arg.cmdOptionExists("-p")) {
    rtpPortNum = (unsigned short)std::stoi(parse_arg.getCmdOption("-p"));
    LOG_D("port number: %d\n", rtpPortNum);
  } else {
    LOG_E("missing option: -p\n");
    LOG_E("Try \'%s -h\' for information/usage\n", argv[0]);
    exit(0);
  }

#if 0
  // Create 'groupsocks' for RTP and RTCP:
  char const* destinationAddressStr
#ifdef USE_IPV6_MULTICAST
#ifdef USE_SSM
    = "FF3E::FFFF:2A2A";
#else
    = "FF1E::FFFF:2A2A";
#endif
#else
#ifdef USE_SSM
    = "232.255.42.42";
#else
    = "239.255.42.42";
#endif
#endif

#endif

  // Note: This is a multicast address.  If you wish to stream using
  // unicast instead, then replace this string with the unicast address
  // of the (single) destination.  (You may also need to make a similar
  // change to the receiver program.)

  // const unsigned short rtpPortNum = 1234;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 7; // low, in case routers don't admin scope

  NetAddressList destinationAddresses(destinationAddressStr);
  struct sockaddr_storage destinationAddress;
  copyAddress(destinationAddress, destinationAddresses.firstAddress());

  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);

  Groupsock rtpGroupsock(*env, destinationAddress, rtpPort, ttl);
  Groupsock rtcpGroupsock(*env, destinationAddress, rtcpPort, ttl);
#ifdef USE_SSM
  rtpGroupsock.multicastSendOnly();
  rtcpGroupsock.multicastSendOnly();
#endif

  // Create an appropriate 'RTP sink' from the RTP 'groupsock':
  videoSink =
    SimpleRTPSink::createNew(*env, &rtpGroupsock, 33, 90000, "video", "MP2T",
			     1, True, False /*no 'M' bit*/);

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidth = 5000; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
#ifdef IMPLEMENT_RTSP_SERVER
  RTCPInstance* rtcp =
#endif
    RTCPInstance::createNew(*env, &rtcpGroupsock,
			    estimatedSessionBandwidth, CNAME,
			    videoSink, NULL /* we're a server */, isSSM);
  // Note: This starts RTCP running automatically

#ifdef IMPLEMENT_RTSP_SERVER
  RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554);
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }
  ServerMediaSession* sms
    = ServerMediaSession::createNew(*env, "testStream", inputFileName,
		   "Session streamed by \"testMPEG2TransportStreamer\"",
					   isSSM);
  sms->addSubsession(PassiveServerMediaSubsession::createNew(*videoSink, rtcp));
  rtspServer->addServerMediaSession(sms);
  announceURL(rtspServer, sms);
#endif

  // Finally, start the streaming:
  *env << "Beginning streaming...\n";
  play();

  env->taskScheduler().doEventLoop(); // does not return

  return 0; // only to prevent compiler warning
}

void afterPlaying(void* /*clientData*/) {
  *env << "...done reading from file\n";

  videoSink->stopPlaying();
  Medium::close(videoSource);
  // Note that this also closes the input file that this source read from.

  play();
}

void play() {
  unsigned const inputDataChunkSize
    = TRANSPORT_PACKETS_PER_NETWORK_PACKET*TRANSPORT_PACKET_SIZE;

  // Open the input file as a 'byte-stream file source':
  ByteStreamFileSource* fileSource
    = ByteStreamFileSource::createNew(*env, inputFileName, inputDataChunkSize);
  if (fileSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
	 << "\" as a byte-stream file source\n";
    exit(1);
  }

  // Create a 'framer' for the input source (to give us proper inter-packet gaps):
  videoSource = MPEG2TransportStreamFramer::createNew(*env, fileSource);

  // Finally, start playing:
  *env << "Beginning to read from file...\n";
  LOG_I("ts file: \e[1;33m%s\e[0m [\e[1;34m%ld\e[0m]\n", inputFileName, count++);
  LOG_I("url    : \e[1;33m%s\e[0m:\e[1;33m%d\e[0m\n", destinationAddressStr, rtpPortNum);
  videoSink->startPlaying(*videoSource, afterPlaying, videoSink);
}
