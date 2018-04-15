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

#include "adapationtobasco.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TobascoAlgorithm");
NS_OBJECT_ENSURE_REGISTERED(TobascoAlgorithm);

TobascoAlgorithm::TobascoAlgorithm(const videoData &videoData,
                                   const playbackData &playbackData,
                                   const bufferData &bufferData,
                                   const throughputData &throughput) : AdaptationAlgorithm(videoData, playbackData, bufferData, throughput),
                                                                       m_a1(0.75),
                                                                       m_a2(0.35),
                                                                       m_a3(0.55),
                                                                       m_a4(0.75),
                                                                       m_a5(0.90),
                                                                       m_bMin(m_videoData.segmentDuration * 2),
                                                                       m_bLow(m_videoData.segmentDuration * 3),
                                                                       m_bHigh(m_videoData.segmentDuration * 7),
                                                                       m_bOpt(m_videoData.segmentDuration * 5),
                                                                       m_lastRepIndex(0),
                                                                       m_lastBufferStatus(0),
                                                                       m_runningFastStart(true),
                                                                       m_highestRepIndex(videoData.averageBitrate[0].size() - 1)
{
  NS_LOG_INFO(this);
  NS_ASSERT_MSG(m_highestRepIndex >= 0, "The highest quality representation index should be >= 0");
}

algorithmReply
TobascoAlgorithm::GetNextRep(const int64_t segmentCounter,
                             const int64_t clientId,
                             int64_t extraParameter,
                             int64_t extraParameter2)
{
  algorithmReply answer;
  int64_t decisionCase = 0;
  int64_t delayDecision = 0;
  int64_t nextRepIndex = 0;
  int64_t bDelay = 0;
  const int64_t timeNow = Simulator::Now().GetMicroSeconds();
  answer.decisionTime = timeNow;
  int64_t bufferNow = 0;
  if (segmentCounter > 0)
  {
    nextRepIndex = m_lastRepIndex;
    bufferNow = m_bufferData.bufferLevelNew.back() - (timeNow - m_throughput.transmissionEnd.back());
    double nextHighestRepBitrate;

    if (m_lastRepIndex < m_highestRepIndex)
      nextHighestRepBitrate = (m_videoData.averageBitrate.at(m_videoData.userInfo.at(segmentCounter)).at(m_lastRepIndex + 1));
    else
      nextHighestRepBitrate = (m_videoData.averageBitrate.at(m_videoData.userInfo.at(segmentCounter)).at(m_lastRepIndex));

    bool qualified = extraParameter > 0 ? ((m_videoData.averageBitrate.at(m_videoData.userInfo.at(segmentCounter)).at(m_lastRepIndex)) <= m_a1 * extraParameter) : true;

    if (m_runningFastStart && m_lastRepIndex != m_highestRepIndex && bufferNow >= m_lastBufferStatus && qualified)
    {
      if (bufferNow < m_bMin)
      {
        if (nextHighestRepBitrate <= (m_a2 * extraParameter))
        {
          decisionCase = 1;
          nextRepIndex = m_lastRepIndex + 1;
        }
      }
      else if (bufferNow < m_bLow)
      {
        if (nextHighestRepBitrate <= (m_a3 * extraParameter))
        {
          decisionCase = 2;
          nextRepIndex = m_lastRepIndex + 1;
        }
      }
      else
      {
        if (nextHighestRepBitrate < (m_a4 * extraParameter))
        {
          decisionCase = 3;
          nextRepIndex = m_lastRepIndex + 1;
        }
        if (bufferNow > m_bHigh)
        {
          delayDecision = 4;
          bDelay = m_bHigh - m_videoData.segmentDuration;
        }
      }
    }
    else
    {
      m_runningFastStart = false;
      if (bufferNow < m_bMin)
      {
        decisionCase = 11;
        nextRepIndex = 0;
      }
      else if (bufferNow < m_bLow)
      {
        double diff = extraParameter2 != 0 ? m_a4 : 1.0;
        double lastSegmentThroughput = (m_videoData.segmentSize.at(m_videoData.userInfo.at(segmentCounter - 1)).at(m_lastRepIndex).at(segmentCounter - 1)) / ((double)(m_throughput.transmissionEnd.at(segmentCounter - 1) - m_throughput.transmissionStart.at(segmentCounter - 1)) / 1000000.0);
        if ((m_lastRepIndex != 0) && (m_videoData.averageBitrate.at(m_videoData.userInfo.at(segmentCounter)).at(m_lastRepIndex) >= diff * lastSegmentThroughput))
        {
          for (int i = m_highestRepIndex; i >= 0; i--)
          {
            if (m_videoData.averageBitrate.at(m_videoData.userInfo.at(segmentCounter)).at(m_lastRepIndex) >= diff * lastSegmentThroughput)
            {
              continue;
            }
            else
            {
              nextRepIndex = i;
              break;
            }
          }
          if (nextRepIndex >= m_lastRepIndex)
          {
            nextRepIndex = m_lastRepIndex - 1;
            decisionCase = 5;
          }
          else
          {
            nextRepIndex = m_lastRepIndex;
            decisionCase = 6;
          }
        }
      }
      else if (bufferNow < m_bHigh)
      {
        if ((m_lastRepIndex == m_highestRepIndex) || (nextHighestRepBitrate >= m_a5 * extraParameter))
        {
          delayDecision = 2;
          bDelay = (int64_t)(std::max(bufferNow - m_videoData.segmentDuration, m_bOpt));
          decisionCase = 7;
        }
        else
        {
          nextRepIndex = m_lastRepIndex;
          decisionCase = 8;
        }
      }
      else
      {
        if ((m_lastRepIndex == m_highestRepIndex) || (nextHighestRepBitrate >= m_a5 * extraParameter))
        {
          delayDecision = 3;
          bDelay = (int64_t)(std::max(bufferNow - m_videoData.segmentDuration, m_bOpt));
          decisionCase = 9;
        }
        else
        {
          decisionCase = 10;
          nextRepIndex = m_lastRepIndex + 1;
        }
      }
    }
  }
  else
  {
    decisionCase = 0;
    nextRepIndex = m_lastRepIndex;
  }

  if (segmentCounter >= 0 && delayDecision != 0)
  {
    if (bDelay > bufferNow)
    {
      bDelay = 0;
    }
    else
    {
      bDelay = (uint64_t)(bufferNow - bDelay);
    }
  }
  m_lastBufferStatus = bufferNow;
  m_lastRepIndex = nextRepIndex;
  answer.nextRepIndex = nextRepIndex;
  answer.nextDownloadDelay = bDelay;
  answer.decisionCase = decisionCase;
  answer.delayDecisionCase = delayDecision;
  answer.estimateTh = extraParameter;

  return answer;
}

} // namespace ns3