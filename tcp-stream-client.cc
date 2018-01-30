/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2016 Technische Universitaet Berlin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "tcp-stream-client.h"
#include <math.h>
#include <sstream>
#include <stdexcept>
#include <stdlib.h>
#include "ns3/global-value.h"
#include <ns3/core-module.h>
#include "tcp-stream-server.h"
#include <unistd.h>
#include <iterator>
#include <numeric>
#include <iomanip>
#include <ctime>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstring>
#include <errno.h>
#include <algorithm>

namespace ns3 {

std::deque<PhyRxStatsCalculator::Time_Tbs> phy_stats;
bool firstOfBwEstimate = true;
double bandwidthEstimate = 0.0;
double bandwidthEstimate_temp1 = 0.0;
double bandwidthEstimate_temp2 = 0.0;
double alpha = 0.1;
double beta = 0.0;
static double lastEndTime = 0.0;
template <typename T>
std::string ToString(T val)
{
    std::stringstream stream;
    stream << val;
    return stream.str();
}


NS_LOG_COMPONENT_DEFINE ("TcpStreamClientApplication");
NS_OBJECT_ENSURE_REGISTERED (TcpStreamClient);

void
TcpStreamClient::Controller (controllerEvent event)
{
  NS_LOG_FUNCTION (this);
  if (state == initial)
  {
    RequestRepIndex ();
    state = downloading;
    Send(m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter));
    return;
  }
  if (state == downloading)
  {
    PlaybackHandle();
    if (m_currentPlaybackIndex <= m_lastSegmentIndex)
        {
          m_segmentCounter++;

          RequestRepIndex ();
          state = downloadingPlaying;
          Send(m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter));
        }
      else
        {
          state = playing;
        }
      controllerEvent ev = playbackFinished;
      Simulator::Schedule (MicroSeconds(m_videoData.segmentDuration), &TcpStreamClient::Controller, this, ev);
      return;
  }
  else if (state == downloadingPlaying)
  {
    if (event == downloadFinished)
    {
      if (m_segmentCounter < m_lastSegmentIndex)
      {
        m_segmentCounter++;
        RequestRepIndex ();
      }
      if (m_bDelay > 0 && m_segmentCounter <= m_lastSegmentIndex)
      {
        state = playing;
        controllerEvent ev = irdFinished;
        Simulator::Schedule (MicroSeconds (m_bDelay), &TcpStreamClient::Controller, this, ev);
      }
      else if (m_segmentCounter == m_lastSegmentIndex)
      {
        state = playing;
      }
      else
      {
        Send(m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter));
      }
    }
    else if (event == playbackFinished)
    {
      if (!PlaybackHandle())
      {
        controllerEvent ev = playbackFinished;
        Simulator::Schedule (MicroSeconds (m_videoData.segmentDuration), &TcpStreamClient::Controller, this, ev);
      }
      else
      {
        state = downloading;
      }
    }
    return;
  }
  else if (state == playing)
  {
    if (event == irdFinished)
    {
      state = downloadingPlaying;
      Send(m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter));
    }
    else if (event == playbackFinished && m_currentPlaybackIndex < m_lastSegmentIndex)
    {
      PlaybackHandle();
      controllerEvent ev = playbackFinished;
      Simulator::Schedule (MicroSeconds (m_videoData.segmentDuration), &TcpStreamClient::Controller, this, ev);
    }
    else if (event == playbackFinished && m_currentPlaybackIndex == m_lastSegmentIndex)
    {
      PlaybackHandle ();
      state = terminal;
      StopApplication ();
    }
    return;
  }
}

TypeId
TcpStreamClient::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpStreamClient")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<TcpStreamClient> ()
    .AddAttribute ("RemoteAddress",
                   "The destination Address of the outbound packets",
                   AddressValue (),
                   MakeAddressAccessor (&TcpStreamClient::m_peerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("RemotePort",
                   "The destination port of the outbound packets",
                   UintegerValue (0),
                   MakeUintegerAccessor (&TcpStreamClient::m_peerPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("SegmentDuration",
                   "The duration of a segment in nanoseconds",
                   UintegerValue (2000000),
                   MakeUintegerAccessor (&TcpStreamClient::m_segmentDuration),
                   MakeUintegerChecker<uint64_t> ())
    .AddAttribute ("SimulationId",
                   "The ID of the current simulation, for logging purposes",
                   UintegerValue (0),
                   MakeUintegerAccessor (&TcpStreamClient::m_simulationId),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NumberOfClients",
                   "The total number of clients for this simulation, for logging purposes",
                   UintegerValue (1),
                   MakeUintegerAccessor (&TcpStreamClient::m_numberOfClients),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("ClientId",
                   "The ID of the this client object, for logging purposes",
                   UintegerValue (0),
                   MakeUintegerAccessor (&TcpStreamClient::m_clientId),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

TcpStreamClient::TcpStreamClient ()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  m_data = 0;
  m_dataSize = 0;
  state = initial;

  m_currentRepIndex = 0;
  m_segmentCounter = 0;
  m_bDelay = 0;
  m_bytesReceived = 0;
  m_segmentsInBuffer = 0;
  m_bufferUnderrun = false;
  m_currentPlaybackIndex = 0;
  //m_currentUserStatus = 0;
}

void
TcpStreamClient::Initialise (std::string algorithm, uint16_t clientId, const Ptr<PhyRxStatsCalculator> ccrossLayerInfo)
{
  NS_LOG_FUNCTION (this);
  cm_crossLayerInfo  = ccrossLayerInfo;
  m_videoData.segmentDuration = m_segmentDuration;
  if (ReadInBitrateValues() == -1)
    {
      NS_LOG_ERROR ("Opening test bitrate file failed. Terminating.\n");
      Simulator::Stop ();
      Simulator::Destroy ();
    }

  m_lastSegmentIndex = (int64_t) m_videoData.segmentSize[0][0].size() -1;
  m_highestRepIndex = m_videoData.averageBitrate[0].size () -1 ;

  if (algorithm == "tobasco")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      //bandwidthAlgo = new BandwidthAvgInTimeAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthLongAvgAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new BufferCleanAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new TobascoAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
    else if(algorithm =="tobasco2")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthCrosslayerAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new BufferCleanAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new TobascoAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
  else if (algorithm == "tomato")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthLongAvgAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new bufferAdaptiveAlgorithm (m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new TomatoAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
    else if (algorithm == "festive")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthHarmonicAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new BufferCleanAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new FestiveAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
    else if (algorithm == "constbitrate")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthHarmonicAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new BufferCleanAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new constbitrateAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
    else if (algorithm == "constbitrate2")
    {
      userinfoAlgo = new UserPredictionAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bandwidthAlgo = new BandwidthHarmonicAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      bufferAlgo = new BufferCleanAlgorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
      algo = new constbitrate2Algorithm(m_videoData, m_playbackData, m_bufferData, m_throughput);
    }
  else
    {
      NS_LOG_ERROR ("Invalid algorithm name entered. Terminating.");
      StopApplication ();
      Simulator::Stop ();
      Simulator::Destroy ();
    }

  m_algoName = algorithm;

  InitializeLogFiles (ToString(m_simulationId), ToString(m_clientId), ToString (m_numberOfClients));

}

TcpStreamClient::~TcpStreamClient()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;

  delete algo;
  delete userinfoAlgo;
  delete bandwidthAlgo;
  delete bufferAlgo;
  algo = NULL;
  userinfoAlgo=NULL;
  bandwidthAlgo=NULL;
  bufferAlgo=NULL;
  delete [] m_data;
  m_data = 0;
  m_dataSize = 0;
}
algorithmReply
TcpStreamClient::UptoQoE(algorithmReply answer) 
{
  uint32_t RepLevel = answer.nextRepIndex;
  uint32_t m_highestQoELevel = (RepLevelToQoE3D.size() - 1);
  NS_ASSERT_MSG(RepLevel <= m_highestQoELevel,  "The choosen RepLevel is higher than the index of RepToQoE ");
  if (RepLevel == 0){
    //std::cout <<"###########"<< updateRepLevel<<"\n";
    answer.nextRepIndex = 0;//10M repLevelIndex
    answer.QoE3D = 3.2;//10M repLevelBW
     return answer;
  }
  else
  {
    std::vector<double> RepLevelToQoE3D_temp = RepLevelToQoE3D;
    RepLevelToQoE3D_temp.resize(RepLevel + 1);
    std::vector<double>::iterator it;
    it = find(RepLevelToQoE3D_temp.begin(), RepLevelToQoE3D_temp.end(), *std::max_element(RepLevelToQoE3D_temp.begin(), RepLevelToQoE3D_temp.end()));
    //std::cout <<*it<<"\n";
    answer.nextRepIndex = std::distance(std::begin(RepLevelToQoE3D_temp), it);
    //std::cout << "###########" << updateRepLevel << "\n";
    answer.QoE3D = *it;
    return answer;
  }
}

static void 
GetPhyRate(Ptr<PhyRxStatsCalculator> phy_rx_stats, double interTime)
{
  //std::cout<<"********  interTime  ***** "<<interTime<<"\n";
  uint32_t cum_tbs = 0;
  double phy_throughput = 0;
  phy_stats = phy_rx_stats->GetCorrectTbs();
  double updateTimescale = 0.0;
  double max_bandwidthEstimate = 0.0;
  for (uint64_t i = 0; i < phy_stats.size(); i++)
  {
    cum_tbs += phy_stats.at(i).tbsize;
  }
  //NS_LOG_INFO("cum_tbs      " << cum_tbs << "    timeduration   " << (phy_stats.at(phy_stats.size() - 1).timescale));
  updateTimescale = phy_stats.at(phy_stats.size() - 1).timescale - interTime;
  phy_throughput = static_cast<double>(cum_tbs) * 8000 / (updateTimescale); //bps

  if (phy_stats.at(phy_stats.size() - 1).gama > 0.3)
  {
    alpha = 0.2;
    beta = 0.8;
  }
  else
  {
    alpha = 0.2;
    beta = 0.8;
  }
  if (firstOfBwEstimate)
  {
    bandwidthEstimate = phy_throughput;
    bandwidthEstimate_temp1 = phy_throughput;
    bandwidthEstimate_temp2 = phy_throughput;
    firstOfBwEstimate = false;
  }
  else
  {
    //bandwidthEstimate = alpha * phy_throughput + (1 - alpha) * bandwidthEstimate;
    bandwidthEstimate_temp1 = alpha * phy_throughput + (1 - alpha) * bandwidthEstimate_temp1;
    bandwidthEstimate_temp2 = beta * bandwidthEstimate_temp1 + (1 - beta) * bandwidthEstimate_temp2;
    bandwidthEstimate = 2 * bandwidthEstimate_temp1 - bandwidthEstimate_temp2 + beta / (1 - beta) * (bandwidthEstimate_temp1 - bandwidthEstimate_temp2);
    //bandwidthEstimate = bandwidthEstimate_temp2;
  }
  if (max_bandwidthEstimate < bandwidthEstimate)
  {
    max_bandwidthEstimate = bandwidthEstimate;
  }
  double ave_mcs = 0;
  for (uint64_t i = 0; i < phy_stats.size(); i++)
  {
    ave_mcs = ave_mcs + phy_stats.at(i).mcs;
  }
  ave_mcs = ave_mcs / phy_stats.size();
  if (ave_mcs > 27)
  {
    bandwidthEstimate = max_bandwidthEstimate;
  }
  //bandwidthEstimate = phy_throughput;
}

void
TcpStreamClient::RequestRepIndex ()
{
  NS_LOG_FUNCTION (this);

  userinfoAlgoReply userinfoanswer;
  bandwidthAlgoReply bandwidthanswer;
  bufferAlgoReply bufferanswer;
  algorithmReply answer;

  if (m_algoName == "tobasco")
  {
    std::cout << "m_clientId  " << m_clientId << "  m_downloadRequest   " << m_downloadRequestSent / (double)1000000 << "   m_transmissionStart    " << m_transmissionStartReceivingSegment / (double)1000000 << "  m_transmissionEnd    " << m_transmissionEndReceivingSegment / (double)1000000 << "\n";
    double interTime = m_downloadRequestSent / (double)1000000 - lastEndTime;
    //Simulator::Schedule(MicroSeconds((double)m_downloadRequestSent), &GetPhyRate, cm_crossLayerInfo);
    Simulator::Schedule(MicroSeconds((double)100), &GetPhyRate, cm_crossLayerInfo, interTime);
    lastEndTime = m_transmissionEndReceivingSegment / (double)1000000;
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId ,0, 0);
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId  , 0, 0);
    bufferanswer = bufferAlgo->BufferAlgo (m_segmentCounter, m_clientId , 0, 10000000);
    //answer = algo->GetNextRep ( m_segmentCounter, m_clientId , bandwidthanswer.bandwidthEstimate, 0);
    answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthEstimate, 0);
    std::cout << "At time " << answer.decisionTime / 1000000.0 << "clientId" << m_clientId << " choose  answer.nextRepIndex  " << answer.nextRepIndex << "    bandwidthEstimate  " << bandwidthEstimate / 1000000 << "Mbps"
              << "\n";
    answer = UptoQoE(answer);
  }
  else if(m_algoName == "tobasco2")
  {
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId ,0, 0);
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId , bandwidthEstimate, 0);
    bufferanswer = bufferAlgo->BufferAlgo (m_segmentCounter, m_clientId , 0, 10000000);
    answer = algo->GetNextRep ( m_segmentCounter, m_clientId , bandwidthanswer.bandwidthEstimate, 0);
    answer = UptoQoE(answer);
  }
  else if (m_algoName == "tomato")
  {
    std::cout << "m_clientId  " << m_clientId << "  m_downloadRequest   " << m_downloadRequestSent / (double)1000000 << "   m_transmissionStart    " << m_transmissionStartReceivingSegment / (double)1000000 << "  m_transmissionEnd    " << m_transmissionEndReceivingSegment / (double)1000000 << "\n";
    double interTime = m_downloadRequestSent / (double)1000000 - lastEndTime;
    //Simulator::Schedule(MicroSeconds((double)m_downloadRequestSent), &GetPhyRate, cm_crossLayerInfo);
    Simulator::Schedule(MicroSeconds((double)100), &GetPhyRate, cm_crossLayerInfo, interTime);
    lastEndTime = m_transmissionEndReceivingSegment / (double)1000000;
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId, 0, 0); 
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId, 0, 0);
    bufferanswer = bufferAlgo->BufferAlgo(m_segmentCounter, m_clientId, 0, 10000000);
    //answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthanswer.bandwidthEstimate, 0);
    answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthEstimate, 0);
    std::cout <<"At time "<<answer.decisionTime / 1000000.0<<"clientId"<<m_clientId<< " choose  answer.nextRepIndex  " << answer.nextRepIndex << "    bandwidthEstimate  " << bandwidthEstimate/1000000<<"Mbps"<<"\n";
    answer = UptoQoE(answer);
  }
  else if (m_algoName == "festive")
  {
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId, 0, 0);
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId, 0, 0);
    bufferanswer = bufferAlgo->BufferAlgo(m_segmentCounter, m_clientId, 0, 10000000);
    answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthanswer.bandwidthEstimate, 0);
    answer = UptoQoE(answer);
  }
  else if (m_algoName == "constbitrate") //constant bitrate
  {
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId, 0, 0);
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId, 0, 0);
    bufferanswer = bufferAlgo->BufferAlgo(m_segmentCounter, m_clientId, 0, 10000000);
    int64_t constRepIndex = 9;//setRepIndex
    answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthanswer.bandwidthEstimate, constRepIndex); 
  }
  else if (m_algoName == "constbitrate2") //constant bitrate_background
  {
    userinfoanswer = userinfoAlgo->UserinfoAlgo(m_segmentCounter, m_clientId, 0, 0);
    bandwidthanswer = bandwidthAlgo->BandwidthAlgo(m_segmentCounter, m_clientId, 0, 0);
    bufferanswer = bufferAlgo->BufferAlgo(m_segmentCounter, m_clientId, 0, 10000000);
    int64_t constRepIndex = 0;//setRepIndex
    answer = algo->GetNextRep(m_segmentCounter, m_clientId, bandwidthanswer.bandwidthEstimate, constRepIndex); 
  }
  else
  {
    NS_LOG_ERROR ("Invalid algorithm name entered. Terminating.");
    StopApplication ();
    Simulator::Stop ();
    Simulator::Destroy ();
  }

  m_videoData.repIndex.push_back(answer.nextRepIndex);
  m_currentRepIndex = answer.nextRepIndex;

  NS_ASSERT_MSG (answer.nextRepIndex <= m_highestRepIndex, "The algorithm returned a representation index that's higher than the maximum");
/*
  NS_LOG_DEBUG("***At Time :" << std::fixed << std::setprecision(3) << answer.decisionTime / 1000000.0
                              << ", Id " << m_clientId
                              << ", Index " << m_segmentCounter
                              << ", Rep " << m_currentRepIndex
                              << ", EstimateBW " << std::fixed << std::setprecision(3) << answer.estimateTh / 1000000.0 << "Mbps"
                              << ", Dealy " << std::fixed << std::setprecision(3) << answer.nextDownloadDelay / 1000000.0 << "Sec"
                              << "  ***");
*/
  m_playbackData.playbackIndex.push_back (answer.nextRepIndex);
  m_bDelay = answer.nextDownloadDelay;
  LogAdaptation (answer);
}

template <typename T> void
TcpStreamClient::Send(T & message)
{
  NS_LOG_FUNCTION (this);
  PreparePacket(message);
  Ptr<Packet> p;
  p = Create<Packet> (m_data, m_dataSize);
  m_downloadRequestSent = Simulator::Now ().GetMicroSeconds () ;
  m_socket->Send (p);
}

void
TcpStreamClient::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  if (m_bytesReceived == 0)
    {
      m_transmissionStartReceivingSegment = Simulator::Now ().GetMicroSeconds () ;
    }
  uint32_t packetSize;
  while ( (packet = socket->Recv ()) )
    {
      packetSize = packet->GetSize ();
      m_bytesReceived += packetSize;
      LogThroughput(packetSize);
      if (m_bytesReceived == m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter))
      {
        SegmentReceivedHandle();
        }
    }
}

std::string
TcpStreamClient::ChoseInfoPath(int64_t infoindex)
{
  NS_LOG_FUNCTION (this);
  switch (infoindex)
  {/*
      case 0:infoStatusTemp="4K_View_0.txt";break;
      case 1: infoStatusTemp="4K_View_1.txt";break;
      case 2: infoStatusTemp="4K_View_2.txt";break;
      case 3: infoStatusTemp="4K_View_3.txt";break;
      case 4: infoStatusTemp="4K_View_4.txt";break;
      case 5: infoStatusTemp="4K_View_5.txt";break;
      default: infoStatusTemp="4K_View_0.txt";break;*/
  case 1:
    infoStatusTemp = "SegmentSize_360s.txt";
    break;
  default:
    infoStatusTemp = "SegmentSize_360s.txt";
    break;
   }

   return infoStatusTemp;
}
void
TcpStreamClient::GetInfo()
{
  //std::ifstream myinfo("UserInfo.txt");
  std::ifstream myinfo("UserInfo_360s.txt");
  for (int64_t s; myinfo >> s;)
    m_videoData.userInfo.push_back(s);
}

int
TcpStreamClient::ReadInBitrateValues()
{
  NS_LOG_FUNCTION (this);

  GetInfo(); //get usermovement infomation 

  for (int64_t i=0;i<6;i++)//view 012345
  {
      std::ifstream myfile;

      segmentSizeFile=ChoseInfoPath(i);

      myfile.open (segmentSizeFile.c_str());

      if (!myfile)
      {
        return -1;
      }
      std::string temp;
      int64_t averageByteSizeTemp = 0;

      while (std::getline(myfile, temp))
      {
        if (temp.empty()) break;
        std::istringstream buffer(temp);
        std::vector<int64_t> line((std::istream_iterator<int64_t>(buffer)),std::istream_iterator<int64_t>());
        comb.push_back(line);
        averageByteSizeTemp = (int64_t) std::accumulate ( line.begin(), line.end(), 0.0)/line.size();
        avBit.push_back((8.0 * averageByteSizeTemp) / (m_videoData.segmentDuration / 1000000.0));//averagebitrate: bps
        line.clear();
      }
      m_videoData.segmentSize.push_back(comb);
      m_videoData.averageBitrate.push_back(avBit);
      comb.clear();
      avBit.clear();
  }

  NS_ASSERT_MSG (!m_videoData.segmentSize.empty(), "No segment sizes read from file.");
  return 1;
}

void
TcpStreamClient::SegmentReceivedHandle()
{
  NS_LOG_FUNCTION (this);
  m_transmissionEndReceivingSegment = Simulator::Now ().GetMicroSeconds () ;


  m_bufferData.timeNow.push_back (m_transmissionEndReceivingSegment);
  if (m_segmentCounter > 0)
    { //if a buffer underrun is encountered, the old buffer level will be set to 0, because the buffer can not be negative
    m_bufferData.bufferLevelOld.push_back (std::max (m_bufferData.bufferLevelNew.back () -
                                          (m_transmissionEndReceivingSegment - m_throughput.transmissionEnd.back()), (int64_t)0));
    }
  else //first segment
    {
      m_bufferData.bufferLevelOld.push_back (0);
    }
  m_bufferData.bufferLevelNew.push_back (m_bufferData.bufferLevelOld.back () + m_videoData.segmentDuration);
  
  m_throughput.bytesReceived.push_back(m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter));
  m_throughput.transmissionStart.push_back(m_transmissionStartReceivingSegment);
  m_throughput.transmissionRequested.push_back(m_downloadRequestSent);
  m_throughput.transmissionEnd.push_back(m_transmissionEndReceivingSegment);

  LogDownload ();

  LogBuffer();

  m_segmentsInBuffer++;
  m_bytesReceived = 0;
  if (m_segmentCounter == m_lastSegmentIndex)
    {
      m_bDelay = 0;
    }
  controllerEvent event = downloadFinished;
  Controller (event);
}

bool
TcpStreamClient::PlaybackHandle()
{
  NS_LOG_FUNCTION (this);
  int64_t timeNow = Simulator::Now ().GetMicroSeconds () ;

  if (m_segmentsInBuffer == 0 && m_currentPlaybackIndex < m_lastSegmentIndex && !m_bufferUnderrun)
    {
      m_bufferUnderrun = true;
      bufferUnderrunLog << std::setfill (' ') << std::setw (9) << timeNow /(double)1000000 << " ";
      bufferUnderrunLog.flush ();
      return true;
    }
  else if (m_segmentsInBuffer > 0)
    {
      if (m_bufferUnderrun)
        {
          m_bufferUnderrun = false;
          bufferUnderrunLog << std::setfill (' ') << std::setw (9) << timeNow /(double)1000000 <<"\n";
          bufferUnderrunLog.flush ();
        }
      m_playbackData.playbackStart.push_back (timeNow);
      LogPlayback ();
      m_segmentsInBuffer--;
      m_currentPlaybackIndex++;
      return false;
    }

    return true;
}

void
TcpStreamClient::SetRemote (Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = ip;
  m_peerPort = port;
}

void
TcpStreamClient::SetRemote (Ipv4Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = Address (ip);
  m_peerPort = port;
}

void
TcpStreamClient::SetRemote (Ipv6Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = Address (ip);
  m_peerPort = port;
}

void
TcpStreamClient::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
TcpStreamClient::StartApplication (void)
{
  NS_LOG_FUNCTION (this);
  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::TcpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
        {
          m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
      else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
        {
          m_socket->Connect (Inet6SocketAddress (Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
      m_socket->SetConnectCallback (
        MakeCallback (&TcpStreamClient::ConnectionSucceeded, this),
        MakeCallback (&TcpStreamClient::ConnectionFailed, this));
      m_socket->SetRecvCallback (MakeCallback (&TcpStreamClient::HandleRead, this));
    }
}

void
TcpStreamClient::StopApplication ()
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }
  downloadLog.close ();
  playbackLog.close ();
  adaptationLog.close ();
  bufferLog.close ();
  throughputLog.close ();
  bufferUnderrunLog.close ();
}


template <typename T> void
TcpStreamClient::PreparePacket (T & message)
{
  NS_LOG_FUNCTION (this << message);
  std::ostringstream ss;
  ss << message;
  ss.str();
  uint32_t dataSize = ss.str().size () + 1;

  if (dataSize != m_dataSize)
    {
      delete [] m_data;
      m_data = new uint8_t [dataSize];
      m_dataSize = dataSize;
    }
  memcpy (m_data, ss.str().c_str (), dataSize);
}

void
TcpStreamClient::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("Tcp Stream Client connection succeeded");
  controllerEvent event = init;
  Controller (event);
}

void
TcpStreamClient::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  NS_LOG_LOGIC ("Tcp Stream Client connection failed");
}

void
TcpStreamClient::LogThroughput(uint32_t packetSize)
{
  NS_LOG_FUNCTION (this);

  throughputLog << std::setfill(' ') << std::setw(8)
                << std::fixed << std::setprecision(3) << Simulator::Now().GetMicroSeconds() / (double)1000000
                << std::setfill(' ') << std::setw(10)
                << packetSize
                << "\n";
  throughputLog.flush ();
}

void
TcpStreamClient::LogDownload()
{
  NS_LOG_FUNCTION (this);

  double Throughput = 8 * m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter) / (m_transmissionEndReceivingSegment / (double)1000000 - m_transmissionStartReceivingSegment / (double)1000000);

  downloadLog << std::setfill(' ') << std::setw(5)
              << m_segmentCounter
              << std::setfill(' ') << std::setw(10)
              << std::fixed << std::setprecision(3) << m_downloadRequestSent / (double)1000000
              << std::setfill(' ') << std::setw(9)
              << std::fixed << std::setprecision(3) << m_transmissionStartReceivingSegment / (double)1000000
              << std::setfill(' ') << std::setw(9)
              << std::fixed << std::setprecision(3) << m_transmissionEndReceivingSegment / (double)1000000
              << std::setfill(' ') << std::setw(10)
              << m_videoData.segmentSize.at(m_videoData.userInfo.at(m_segmentCounter)).at(m_currentRepIndex).at(m_segmentCounter)
              << std::setfill(' ') << std::setw(12)
              << std::fixed << std::setprecision(0) << Throughput << "\t"
              << std::setfill(' ') << std::setw(5)
              << m_videoData.userInfo.at(m_segmentCounter)
              << "\n";
  downloadLog.flush ();
}

void
TcpStreamClient::LogBuffer()
{
  NS_LOG_FUNCTION (this);
  bufferLog << std::setfill(' ') << std::setw(8)
            << std::fixed << std::setprecision(3) << m_transmissionEndReceivingSegment / (double)1000000
            << std::setfill(' ') << std::setw(9)
            << std::fixed << std::setprecision(3) << m_bufferData.bufferLevelOld.back() / (double)1000000
            << std::setfill(' ') << std::setw(10)
            << std::fixed << std::setprecision(3) << m_bufferData.bufferLevelNew.back() / (double)1000000
            << "\n";
  bufferLog.flush ();
}

void
TcpStreamClient::LogAdaptation(algorithmReply answer)
{
  NS_LOG_FUNCTION (this);
  adaptationLog << std::setfill(' ') << std::setw(5)
                << m_segmentCounter
                << std::setfill(' ') << std::setw(9)
                << m_currentRepIndex
                << std::setfill(' ') << std::setw(14)
                << std::fixed << std::setprecision(3) << answer.decisionTime / (double)1000000
                << std::setfill(' ') << std::setw(13)
                << std::fixed << std::setprecision(0) << answer.estimateTh
                << std::setfill(' ') << std::setw(4)
                << answer.decisionCase
                << std::setfill(' ') << std::setw(6)
                << answer.delayDecisionCase
                << std::setfill(' ') << std::setw(9)
                << m_videoData.userInfo.at(m_segmentCounter)
                << "\n";
  adaptationLog.flush ();
}

void
TcpStreamClient::LogPlayback()
{
  NS_LOG_FUNCTION (this);
  playbackLog << std::setfill(' ') << std::setw(5)
              << m_currentPlaybackIndex
              << std::setfill(' ') << std::setw(11)
              << std::fixed << std::setprecision(3) << Simulator::Now().GetMicroSeconds() / (double)1000000
              << std::setfill(' ') << std::setw(7)
              << m_playbackData.playbackIndex.at(m_currentPlaybackIndex)
              << std::setfill(' ') << std::setw(10)
              << m_videoData.userInfo.at(m_currentPlaybackIndex)
              << "\n";
  playbackLog.flush ();
}

void
TcpStreamClient::InitializeLogFiles (std::string simulationId, std::string clientId, std::string numberOfClients)
{
  NS_LOG_FUNCTION (this);

  std::string dLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "downloadLog.txt";
  downloadLog.open (dLog.c_str());
  //downloadLog << "SegIndex ReqSent  DoStart  DownEnd  SegSize  eachDownRate UserStatus\n";//
  //downloadLog.flush ();

  std::string pLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "playbackLog.txt";
  playbackLog.open (pLog.c_str());
  //playbackLog << "SegIndex PlayStart RepLevel UserStatus\n";//6 11 7 10
 // playbackLog.flush ();

  std::string aLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "adaptationLog.txt";
  adaptationLog.open (aLog.c_str());
  //adaptationLog << "SegIndex RepLevel DecisionTime EstimateTh Case DCase UserStatus\n";
  //adaptationLog.flush ();

  std::string bLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "bufferLog.txt";
  bufferLog.open (bLog.c_str());
  //bufferLog << "  AtTime BufferLevel BufferNew \n";
  //bufferLog.flush ();

  std::string tLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "throughputLog.txt";
  throughputLog.open (tLog.c_str());
  //throughputLog << "  AtTime  PacketSize/byte \n";
  //throughputLog.flush ();

  std::string buLog = "mylogs/" + m_algoName + "/" +  numberOfClients  + "/sim" + simulationId + "_" + "cl" + clientId + "_"  + "bufferUnderrunLog.txt";
  bufferUnderrunLog.open (buLog.c_str());
  //bufferUnderrunLog << ("StarteTime  EndTime  Sum\n");
  //bufferUnderrunLog.flush ();
}

} // Namespace ns3