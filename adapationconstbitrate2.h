#ifndef CONSTBITRATE_2_ALGORITHM_H
#define CONSTBITRATE_2_ALGORITHM_H

#include "tcp-stream-adaptation-algorithm.h"

namespace ns3 {

class constbitrate2Algorithm : public AdaptationAlgorithm
{
public:
  constbitrate2Algorithm (  const videoData &videoData,
                                                     const playbackData & playbackData,
                                                     const bufferData & bufferData,
                                                     const throughputData & throughput);

  algorithmReply GetNextRep(const int64_t segmentCounter,
                            const int64_t clientId,
                            int64_t extraParameter,
                            int64_t extraParameter2);

private:
  const int64_t m_bufferTargerNumber;//targetBuffer
  const int64_t m_deltaBuffer;  //[m_bufferTargerNumber-m_deltaBuffer, m_bufferTargerNumber+m_deltaBuffer]
  const int64_t m_highestRepIndex;//Highest Reps Index
  const int64_t m_constRepIndex;
};

} // namespace ns3
#endif /* CONSTBITRATE_ALGORITHM_H */
