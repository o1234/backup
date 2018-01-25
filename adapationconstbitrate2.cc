#include "adapationconstbitrate2.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("constbitrate2Algorithm");
NS_OBJECT_ENSURE_REGISTERED (constbitrate2Algorithm);

constbitrate2Algorithm::constbitrate2Algorithm (const videoData &videoData,
                                              const playbackData & playbackData,
                                              const bufferData & bufferData,
                                              const throughputData & throughput) :
  AdaptationAlgorithm (videoData, playbackData, bufferData, throughput),
  m_bufferTargerNumber (10000000),//10s
  m_deltaBuffer(2000000),
  m_highestRepIndex (videoData.averageBitrate[0].size() - 1),
  m_constRepIndex(4)
{
  NS_LOG_INFO (this);
  NS_ASSERT_MSG (m_highestRepIndex >= 0, "The highest quality representation index should be => 0");
}

algorithmReply
constbitrate2Algorithm::GetNextRep(const int64_t segmentCounter,
                                  int64_t clientId,
                                  int64_t extraParameter,//bandwidthEstimate
                                  int64_t extraParameter2)//constRepIndex
{
  algorithmReply answer;
  int64_t timeNow = Simulator::Now().GetMicroSeconds();
  answer.decisionTime = timeNow;
  answer.decisionCase = 0;
  answer.nextRepIndex = m_constRepIndex;
  answer.nextDownloadDelay = 0;
  answer.delayDecisionCase = 0;

  if (extraParameter > 0)
    answer.estimateTh = extraParameter;
  else
    answer.estimateTh = 0.0;

  if (extraParameter2 >= 0 && extraParameter2 <= m_highestRepIndex)
  {
    answer.nextRepIndex = extraParameter2;
    answer.decisionCase = 1;
  }
  else
  {
    answer.nextRepIndex = 0;
    answer.decisionCase = 0;//2;
  }

  if (segmentCounter != 0)
  {
    int64_t lowerBound = m_bufferTargerNumber - m_deltaBuffer;
    int64_t upperBound = m_bufferTargerNumber + m_deltaBuffer;
    int64_t bufferNow = m_bufferData.bufferLevelNew.back() - (timeNow - m_throughput.transmissionEnd.back());
    int64_t randBuf = (int64_t)lowerBound + (std::rand() % (upperBound - (lowerBound) + 1));
    if (bufferNow > randBuf)
    {
      answer.nextDownloadDelay = 0;//bufferNow - randBuf;
      answer.delayDecisionCase = 0;//2;
    }
    else
    {
      answer.nextDownloadDelay = 0;
      answer.delayDecisionCase = 0;//1;
    }
  }
  return answer;
}

} // namespace ns3
