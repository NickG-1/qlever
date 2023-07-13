// Copyright 2015, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Björn Buchhold (buchhold@informatik.uni-freiburg.de)

#include "./FTSAlgorithms.h"

#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "util/HashMap.h"
#include "util/HashSet.h"
#include "util/SuppressWarnings.h"

using std::pair;

// _____________________________________________________________________________
Index::WordEntityPostings FTSAlgorithms::filterByRange(
    const IdRange<WordVocabIndex>& idRange,
    const Index::WordEntityPostings& wepPreFilter) {
  AD_CONTRACT_CHECK(wepPreFilter.wids_.size() == 1);
  AD_CONTRACT_CHECK(wepPreFilter.cids_.size() == wepPreFilter.wids_[0].size());
  AD_CONTRACT_CHECK(wepPreFilter.cids_.size() == wepPreFilter.scores_.size());
  LOG(DEBUG) << "Filtering " << wepPreFilter.cids_.size()
             << " elements by ID range...\n";

  Index::WordEntityPostings wepResult;
  wepResult.cids_.resize(wepPreFilter.cids_.size() + 2);
  wepResult.scores_.resize(wepPreFilter.cids_.size() + 2);
  wepResult.wids_[0].resize(wepPreFilter.cids_.size() + 2);

  size_t nofResultElements = 0;

  for (size_t i = 0; i < wepPreFilter.wids_[0].size(); ++i) {
    // TODO<joka921> proper Ids for the text stuff.
    // The mapping from words that appear in text records to `WordIndex`es is
    // stored in a `Vocabulary` that stores `VocabIndex`es, so we have to
    // convert between those two types.
    // TODO<joka921> Can we make the returned `IndexType` a template parameter
    // of the vocabulary, s.t. we have a vocabulary that stores `WordIndex`es
    // directly?
    if (wepPreFilter.wids_[0][i] >= idRange._first.get() &&
        wepPreFilter.wids_[0][i] <= idRange._last.get()) {
      wepResult.cids_[nofResultElements] = wepPreFilter.cids_[i];
      wepResult.scores_[nofResultElements] = wepPreFilter.scores_[i];
      wepResult.wids_[0][nofResultElements++] = wepPreFilter.wids_[0][i];
    }
  }

  wepResult.cids_.resize(nofResultElements);
  wepResult.scores_.resize(nofResultElements);
  wepResult.wids_[0].resize(nofResultElements);

  AD_CONTRACT_CHECK(wepResult.cids_.size() == wepResult.scores_.size());
  AD_CONTRACT_CHECK(wepResult.cids_.size() == wepResult.wids_[0].size());
  LOG(DEBUG) << "Filtering by ID range done. Result has "
             << wepResult.cids_.size() << " elements.\n";
  return wepResult;
}

// _____________________________________________________________________________
Index::WordEntityPostings FTSAlgorithms::crossIntersect(
    const Index::WordEntityPostings& matchingContextsWep,
    const Index::WordEntityPostings& eBlockWep) {
  // Example:
  // matchingContextsWep.wids_[0]: 3 4 3 4 3
  // matchingContextsWep.cids_: 1 4 5 5 7
  // -----------------------------------
  // eBlockWep.cids_          : 4 5 5 8
  // eBlockWep.eids_          : 2 1 2 1
  // ===================================
  // resultWep.cids_          : 4 5 5 5 5
  // resultWep.wids_[0]          : 4 3 4 3 4
  // resultWep.eids_          : 2 1 2 2 1
  AD_CONTRACT_CHECK(matchingContextsWep.wids_.size() == 1);
  LOG(DEBUG)
      << "Intersection to filter the word-entity postings from a block so that "
      << "only entries remain where the context matches. If there are multiple "
      << "entries with the same cid, then the crossproduct of them remains.\n";
  LOG(DEBUG) << "matchingContextsWep.cids_ size: "
             << matchingContextsWep.cids_.size() << '\n';
  LOG(DEBUG) << "eBlockWep.cids_ size: " << eBlockWep.cids_.size() << '\n';
  Index::WordEntityPostings resultWep;
  // Handle trivial empty case
  if (matchingContextsWep.cids_.empty() || eBlockWep.cids_.empty()) {
    return resultWep;
  }
  resultWep.wids_[0].reserve(eBlockWep.cids_.size());
  resultWep.wids_[0].clear();
  resultWep.cids_.reserve(eBlockWep.cids_.size());
  resultWep.cids_.clear();
  resultWep.eids_.reserve(eBlockWep.cids_.size());
  resultWep.eids_.clear();
  resultWep.scores_.reserve(eBlockWep.cids_.size());
  resultWep.scores_.clear();

  size_t i = 0;
  size_t j = 0;

  while (i < matchingContextsWep.cids_.size() && j < eBlockWep.cids_.size()) {
    while (matchingContextsWep.cids_[i] < eBlockWep.cids_[j]) {
      ++i;
      if (i >= matchingContextsWep.cids_.size()) {
        return resultWep;
      }
    }
    while (eBlockWep.cids_[j] < matchingContextsWep.cids_[i]) {
      ++j;
      if (j >= eBlockWep.cids_.size()) {
        return resultWep;
      }
    }
    while (matchingContextsWep.cids_[i] == eBlockWep.cids_[j]) {
      size_t k = 0;
      while (matchingContextsWep.cids_[i + k] == matchingContextsWep.cids_[i]) {
        // Make sure that we get every combination of eids and wid where their
        // cids match.
        resultWep.wids_[0].push_back(matchingContextsWep.wids_[0][i + k]);
        resultWep.cids_.push_back(eBlockWep.cids_[j]);
        resultWep.eids_.push_back(eBlockWep.eids_[j]);
        resultWep.scores_.push_back(eBlockWep.scores_[j]);
        k++;
        if (i + k >= matchingContextsWep.cids_.size()) {
          break;
        }
      }
      j++;
      if (j >= eBlockWep.cids_.size()) {
        break;
      }
    }
    ++i;
  }
  return resultWep;
}

// _____________________________________________________________________________
Index::WordEntityPostings FTSAlgorithms::crossIntersectKWay(
    const vector<Index::WordEntityPostings>& wepVecs,
    vector<Id>* lastListEids) {
  size_t k = wepVecs.size();
  Index::WordEntityPostings resultWep;
  {
    if (wepVecs[k - 1].cids_.empty()) {
      LOG(DEBUG) << "Empty list involved, no intersect necessary.\n";
      return resultWep;
    }
    LOG(DEBUG) << "K-way intersection of " << k << " lists of sizes: ";
    for (const auto& l : wepVecs) {
      LOG(DEBUG) << l.cids_.size() << ' ';
    }
    LOG(DEBUG) << '\n';
  }

  const bool entityMode = lastListEids != nullptr;

  size_t minSize = std::numeric_limits<size_t>::max();
  if (entityMode) {
    minSize = lastListEids->size();
  } else {
    for (size_t i = 0; i < k; ++i) {
      if (wepVecs[i].cids_.empty()) {
        return resultWep;
      }
      if (wepVecs[i].cids_.size() < minSize) {
        minSize = wepVecs[i].cids_.size();
      }
    }
  }
  AD_CORRECTNESS_CHECK(minSize != std::numeric_limits<size_t>::max());

  resultWep.cids_.reserve(minSize);
  resultWep.scores_.reserve(minSize);
  if (lastListEids) {
    resultWep.eids_.reserve(minSize);
  }
  resultWep.wids_.resize(k);
  for (size_t j = 0; j < k; j++) {
    AD_CONTRACT_CHECK(wepVecs[j].wids_.size() == 1);
    resultWep.wids_[j].reserve(minSize);
  }

  // For intersection, we don't need a PQ.
  // The algorithm:
  // Remember the current context and the length of the streak
  // (i.e. in how many lists was that context found).
  // If the streak reaches k, write the context to the result.
  // Until then, go through lists in a round-robin way and advance until
  // a) the context is found or
  // b) a higher context is found without a match before (reset cur and streak).
  // Stop as soon as one list cannot advance.

  // I think no PQ is needed, because unlike for merge, elements that
  // do not occur in all lists, don't have to be visited in the right order.

  vector<size_t> nextIndices;
  nextIndices.resize(wepVecs.size(), 0);
  TextRecordIndex currentContext = wepVecs[k - 1].cids_[0];
  size_t currentList = k - 1;  // Has the fewest different contexts. Start here.
  size_t streak = 0;
  size_t n = 0;
  while (true) {  // break when one list cannot advance
    size_t thisListSize = wepVecs[currentList].cids_.size();
    if (nextIndices[currentList] == thisListSize) {
      break;
    }
    while (wepVecs[currentList].cids_[nextIndices[currentList]] <
           currentContext) {
      nextIndices[currentList] += 1;
      if (nextIndices[currentList] == thisListSize) {
        break;
      }
    }
    if (nextIndices[currentList] == thisListSize) {
      break;
    }
    TextRecordIndex atId = wepVecs[currentList].cids_[nextIndices[currentList]];
    if (atId == currentContext) {
      if (++streak == k) {
        Score s = 0;
        vector<size_t> currentIndices;
        currentIndices.resize(k, 0);
        for (size_t i = 0; i < k; ++i) {
          size_t index =
              (i == currentList ? nextIndices[i] : nextIndices[i] - 1);
          currentIndices[i] = index;
          if (i != k - 1) {
            s += wepVecs[i].scores_[index];
          }
        }
        int kIndex = k - 1;
        vector<size_t> offsets;
        offsets.resize(k, 0);
        while (kIndex >= 0) {
          if (currentIndices[kIndex] + offsets[kIndex] >=
                  wepVecs[kIndex].cids_.size() ||
              wepVecs[kIndex].cids_[currentIndices[kIndex] + offsets[kIndex]] !=
                  currentContext) {
            offsets[kIndex] = 0;
            kIndex--;
            if (kIndex >= 0) {
              offsets[kIndex]++;
            }
          } else {
            kIndex = k - 1;
            resultWep.cids_.push_back(currentContext);
            if (entityMode) {
              resultWep.eids_.push_back(
                  (*lastListEids)[currentIndices[k - 1] + offsets[k - 1]]);
            }
            resultWep.scores_.push_back(
                s +
                wepVecs[k - 1].scores_[currentIndices[k - 1] + offsets[k - 1]]);
            for (int c = k - 1; c >= 0; c--) {
              resultWep.wids_[c].push_back(
                  wepVecs[c].wids_[0][currentIndices[c] + offsets[c]]);
            }
            n++;
            offsets[kIndex]++;
          }
        }
        // Optimization: The last list will feature the fewest different
        // contexts. After a match, always advance in that list
        currentList = k - 1;
        continue;
      }
    } else {
      streak = 1;
      currentContext = atId;
    }
    nextIndices[currentList] += 1;
    currentList++;
    if (currentList == k) {
      currentList = 0;
    }  // wrap around
  }

  resultWep.cids_.resize(n);
  resultWep.scores_.resize(n);
  if (entityMode) {
    resultWep.eids_.resize(n);
  }
  for (size_t j = 0; j < k; j++) {
    resultWep.wids_[j].resize(n);
  }
  LOG(DEBUG) << "Intersection done. Size: " << resultWep.cids_.size() << "\n";

  return resultWep;
}

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::aggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.eids_.size());
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.scores_.size());
  LOG(DEBUG)
      << "Going from a WordEntityPostings-Element consisting of an entity,"
      << " context, word and score list of size: " << wep.cids_.size()
      << " elements to a table with distinct entities "
      << "and at most " << k << " contexts per entity.\n";

  size_t numOfTerms = wep.wids_.size();

  // The default case where k == 1 can use a map for a O(n) solution
  if (k == 1) {
    aggScoresAndTakeTopContext<WIDTH>(wep, dynResult);
    return;
  }

  // Use a set (ordered) and keep it at size k for the context scores
  // This achieves O(n log k)
  LOG(DEBUG) << "Heap-using case with " << k << " contexts per entity...\n";

  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<Id, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
  using AggMapEntityToSaCS =
      ad_utility::HashMap<Id, std::pair<Score, ScoreAndContextSet>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaCS mapEtSaCS;
  for (size_t i = 0; i < wep.eids_.size(); ++i) {
    vector<WordIndex> wids;
    wids.resize(numOfTerms);
    for (size_t l = 0; l < numOfTerms; l++) {
      wids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
    }
    mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].emplace_back(wids);
    if (!mapEtSaCS.contains(wep.eids_[i])) {
      mapEtSaCS[wep.eids_[i]].second.emplace(wep.scores_[i], wep.cids_[i]);
      mapEtSaCS[wep.eids_[i]].first = 1;
    } else {
      if (mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].size() >= 2) {
        continue;
      }
      mapEtSaCS[wep.eids_[i]].first++;
      ScoreAndContextSet& sacs = mapEtSaCS[wep.eids_[i]].second;
      if (sacs.size() < k || sacs.begin()->first < wep.scores_[i]) {
        if (sacs.size() == k) {
          sacs.erase(*sacs.begin());
        }
        sacs.emplace(wep.scores_[i], wep.cids_[i]);
      }
    }
  }
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaCS.size() + 2);
  for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const Id eid = it->first;
    const Id entityScore = Id::makeFromInt(it->second.first);
    const ScoreAndContextSet& sacs = it->second.second;
    row[1] = entityScore;
    row[2] = eid;
    for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
      const TextRecordIndex cid = itt->second;
      row[0] = Id::makeFromTextRecordIndex(cid);
      const vector<vector<WordIndex>> widsVec =
          mapEaCtW[std::make_pair(eid, cid)];
      for (size_t j = 0; j < widsVec.size(); j++) {
        for (size_t l = 0; l < numOfTerms; l++) {
          row[3 + l] =
              Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
        }
        result.push_back(row);
      }
    }
  }
  *dynResult = std::move(result).toDynamic();

  // The result is NOT sorted due to the usage of maps.
  // Resorting the result is a separate operation now.
  // Benefit 1) it's not always necessary to sort.
  // Benefit 2) The result size can be MUCH smaller than n.
  LOG(DEBUG) << "Done. There are " << dynResult->size()
             << " entity-word-score-context tuples now.\n";
}

template void FTSAlgorithms::aggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep, size_t k, IdTable* dynResult);

// _____________________________________________________________________________
template <typename Row>
void FTSAlgorithms::aggScoresAndTakeTopKContexts(vector<Row>& nonAggRes,
                                                 size_t k, vector<Row>& res) {
  AD_CONTRACT_CHECK(res.empty());
  LOG(DEBUG) << "Aggregating scores from a list of size " << nonAggRes.size()
             << " while keeping the top " << k << " contexts each.\n";

  if (nonAggRes.empty()) return;

  size_t width = nonAggRes[0].size();
  std::ranges::sort(nonAggRes, [width](const Row& l, const Row& r) {
    if (l[0] != r[0]) {
      return l[0] < r[0];
    }
    for (size_t i = 3; i < width; ++i) {
      DISABLE_WARNINGS_GCC_13
      if (l[i] != r[i]) {
        ENABLE_WARNINGS_GCC_13
        return l[i] < r[i];
      }
    }
    return l[1] < r[1];
  });

  res.push_back(nonAggRes[0]);
  size_t contextsInResult = 1;
  for (size_t i = 1; i < nonAggRes.size(); ++i) {
    bool same = false;
    if (nonAggRes[i][0] == res.back()[0]) {
      same = true;
      for (size_t j = 3; j < width; ++j) {
        if (nonAggRes[i][j] != res.back()[j]) {
          same = false;
          break;
        }
      }
    }
    if (same) {
      ++contextsInResult;
      if (contextsInResult <= k) {
        res.push_back(nonAggRes[i]);
      }
    } else {
      // Other

      // update scores on last
      for (size_t j = res.size() - std::min(contextsInResult, k);
           j < res.size(); ++j) {
        assert(j < i);
        assert(j < res.size());
        res[j][1] = Id::makeFromInt(contextsInResult);
      }

      // start with current
      res.push_back(nonAggRes[i]);
      contextsInResult = 1;
    }
  }

  LOG(DEBUG) << "Done. There are " << res.size()
             << " entity-score-context tuples now.\n";
}

template void FTSAlgorithms::aggScoresAndTakeTopKContexts(
    vector<array<Id, 4>>& nonAggRes, size_t k, vector<array<Id, 4>>& res);

template void FTSAlgorithms::aggScoresAndTakeTopKContexts(
    vector<array<Id, 5>>& nonAggRes, size_t k, vector<array<Id, 5>>& res);

template void FTSAlgorithms::aggScoresAndTakeTopKContexts(
    vector<vector<Id>>& nonAggRes, size_t k, vector<vector<Id>>& res);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::aggScoresAndTakeTopContext(
    const Index::WordEntityPostings& wep, IdTable* dynResult) {
  LOG(DEBUG) << "Special case with 1 contexts per entity...\n";
  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<Id, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContext = std::pair<Score, TextRecordIndex>;
  using AggMapEntityToSaC =
      ad_utility::HashMap<Id, std::pair<Score, ScoreAndContext>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaC mapEtSaC;

  size_t numOfTerms = wep.wids_.size();

  for (size_t i = 0; i < wep.eids_.size(); ++i) {
    vector<WordIndex> wids;
    wids.resize(numOfTerms);
    for (size_t l = 0; l < numOfTerms; l++) {
      wids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
    }
    mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].emplace_back(wids);
    if (!mapEtSaC.contains(wep.eids_[i])) {
      mapEtSaC[wep.eids_[i]].second =
          std::make_pair(wep.scores_[i], wep.cids_[i]);
      mapEtSaC[wep.eids_[i]].first = 1;
    } else {
      if (mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].size() >= 2) {
        continue;
      }
      mapEtSaC[wep.eids_[i]].first++;
      ScoreAndContext& sac = mapEtSaC[wep.eids_[i]].second;
      if (sac.first < wep.scores_[i]) {
        sac = std::make_pair(wep.scores_[i], wep.cids_[i]);
      };
    }
  }
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaC.size() + 2);
  for (auto it = mapEtSaC.begin(); it != mapEtSaC.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const ScoreAndContext& sac = it->second.second;
    const Id eid = it->first;
    const Id entityScore = Id::makeFromInt(it->second.first);
    row[0] = Id::makeFromTextRecordIndex(sac.second);
    row[1] = entityScore;
    row[2] = eid;
    const vector<vector<WordIndex>> widsVec =
        mapEaCtW[std::make_pair(eid, sac.second)];
    for (size_t j = 0; j < widsVec.size(); j++) {
      for (size_t l = 0; l < numOfTerms; l++) {
        row[3 + l] =
            Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
      }
      result.push_back(row);
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size()
             << " context-score-entity tuples now.\n";
}

template void FTSAlgorithms::aggScoresAndTakeTopContext<0>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<1>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<2>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<3>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<4>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<5>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<6>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<7>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<8>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<9>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);
template void FTSAlgorithms::aggScoresAndTakeTopContext<10>(
    const Index::WordEntityPostings& wep, IdTable* dynResult);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);

  size_t numOfTerms = wep.wids_.size();

  if (wep.cids_.empty()) {
    return;
  }
  if (kLimit == 1) {
    multVarsAggScoresAndTakeTopContext<WIDTH>(wep, nofVars, dynResult);
  } else {
    // Go over contexts.
    // For each context build a cross product of width 2.
    // Store them in a map, use a pair of id's as key and
    // an appropriate hash function.
    // Use a set (ordered) and keep it at size k for the context scores
    // This achieves O(n log k)
    LOG(DEBUG) << "Heap-using case with " << kLimit
               << " contexts per entity...\n";
    using AggMapContextAndEntityToWord =
        ad_utility::HashMap<std::pair<vector<Id>, TextRecordIndex>,
                            vector<vector<WordIndex>>>;
    using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
    using AggMapEntityToSaCS =
        ad_utility::HashMap<vector<Id>, std::pair<Score, ScoreAndContextSet>>;
    AggMapContextAndEntityToWord mapEaCtW;
    AggMapEntityToSaCS mapEtSaCS;
    vector<Id> entitiesInContext;
    TextRecordIndex currentCid = wep.cids_[0];
    Score cscore = wep.scores_[0];
    vector<WordIndex> cwids;
    cwids.reserve(numOfTerms);
    cwids.resize(numOfTerms);
    for (size_t l = 0; l < numOfTerms; l++) {
      cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][0];
    }

    for (size_t i = 0; i < wep.cids_.size(); ++i) {
      if (wep.cids_[i] == currentCid) {
        entitiesInContext.push_back(wep.eids_[i]);
        // cscore += scores[i];
      } else {
        // Calculate a cross product and add/update the map
        size_t nofPossibilities =
            static_cast<size_t>(pow(entitiesInContext.size(), nofVars));
        for (size_t j = 0; j < nofPossibilities; ++j) {
          vector<Id> key;
          key.reserve(nofVars);
          size_t n = j;
          for (size_t k = 0; k < nofVars; ++k) {
            key.push_back(entitiesInContext[n % entitiesInContext.size()]);
            n /= entitiesInContext.size();
          }
          mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
          if (!mapEtSaCS.contains(key)) {
            mapEtSaCS[key].second.emplace(cscore, currentCid);
            mapEtSaCS[key].first = 1;
          } else {
            if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
              continue;
            }
            mapEtSaCS[key].first++;
            ScoreAndContextSet& sacs = mapEtSaCS[key].second;
            if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
              if (sacs.size() == kLimit) {
                sacs.erase(*sacs.begin());
              }
              sacs.emplace(cscore, currentCid);
            }
          }
        }
        entitiesInContext.clear();
        currentCid = wep.cids_[i];
        cscore = wep.scores_[i];
        for (size_t l = 0; l < numOfTerms; l++) {
          cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
        }
        entitiesInContext.push_back(wep.eids_[i]);
      }
    }
    // Deal with the last context
    // Calculate a cross product and add/update the map
    size_t nofPossibilities =
        static_cast<size_t>(pow(entitiesInContext.size(), nofVars));
    for (size_t j = 0; j < nofPossibilities; ++j) {
      vector<Id> key;
      key.reserve(nofVars);
      size_t n = j;
      for (size_t k = 0; k < nofVars; ++k) {
        key.push_back(entitiesInContext[n % entitiesInContext.size()]);
        n /= entitiesInContext.size();
      }
      mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
      if (!mapEtSaCS.contains(key)) {
        mapEtSaCS[key].second.emplace(cscore, currentCid);
        mapEtSaCS[key].first = 1;
      } else {
        if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
          continue;
        }
        mapEtSaCS[key].first++;
        ScoreAndContextSet& sacs = mapEtSaCS[key].second;
        if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
          if (sacs.size() == kLimit) {
            sacs.erase(*sacs.begin());
          }
          sacs.emplace(cscore, currentCid);
        }
      }
    }
    // Iterate over the map and populate the result.
    IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
    result.reserve(mapEtSaCS.size() + 2);
    for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
      std::vector<ValueId> row;
      row.resize(result.numColumns());
      const Id entityScore = Id::makeFromInt(it->second.first);
      const ScoreAndContextSet& sacs = it->second.second;
      row[1] = entityScore;
      for (size_t k = 0; k < nofVars; ++k) {
        row[k + 2] = it->first[k];  // eid
      }
      for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
        const TextRecordIndex cid = itt->second;
        row[0] = Id::makeFromTextRecordIndex(cid);
        const vector<vector<WordIndex>> widsVec =
            mapEaCtW[std::make_pair(it->first, cid)];
        for (size_t j = 0; j < widsVec.size(); j++) {
          for (size_t l = 0; l < numOfTerms; l++) {
            row[2 + nofVars + l] =
                Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
          }
          result.push_back(row);
        }
      }
    }
    *dynResult = std::move(result).toDynamic();
    LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
  }
}

template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::multVarsAggScoresAndTakeTopContext(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  LOG(DEBUG) << "Special case with 1 contexts per entity...\n";

  size_t numOfTerms = wep.wids_.size();

  // Go over contexts.
  // For each context build a cross product of width 2.
  // Store them in a map, use a pair of id's as key and
  // an appropriate hash function.
  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<vector<Id>, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContext = std::pair<Score, TextRecordIndex>;
  using AggMapEntityToSaC =
      ad_utility::HashMap<vector<Id>, std::pair<Score, ScoreAndContext>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaC mapEtSaC;
  // Note: vector{k} initializes with a single value k, as opposed to
  // vector<Id>(k), which initializes a vector with k default constructed
  // arguments.
  // TODO<joka921> proper Id types
  vector<Id> entitiesInContext;
  TextRecordIndex currentCid = wep.cids_[0];
  Score cscore = wep.scores_[0];
  vector<WordIndex> cwids;
  cwids.resize(numOfTerms);
  for (size_t l = 0; l < numOfTerms; l++) {
    cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][0];
  }
  for (size_t i = 0; i < wep.cids_.size(); ++i) {
    if (wep.cids_[i] == currentCid) {
      entitiesInContext.push_back(wep.eids_[i]);
      // cscore = std::max(cscore, scores[i]);
    } else {
      // Calculate a cross product and add/update the map
      size_t nofPossibilities =
          static_cast<size_t>(pow(entitiesInContext.size(), nofVars));
      for (size_t j = 0; j < nofPossibilities; ++j) {
        vector<Id> key;
        key.reserve(nofVars);
        size_t n = j;
        for (size_t k = 0; k < nofVars; ++k) {
          key.push_back(entitiesInContext[n % entitiesInContext.size()]);
          n /= entitiesInContext.size();
        }
        mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
        if (!mapEtSaC.contains(key)) {
          mapEtSaC[key].second = std::make_pair(cscore, currentCid);
          mapEtSaC[key].first = 1;
        } else {
          if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
            continue;
          }
          mapEtSaC[key].first++;
          ScoreAndContext& sac = mapEtSaC[key].second;
          if (sac.first < cscore) {
            sac = std::make_pair(cscore, currentCid);
          }
        }
      }
      entitiesInContext.clear();
      currentCid = wep.cids_[i];
      cscore = wep.scores_[i];
      for (size_t l = 0; l < numOfTerms; l++) {
        cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
      }
      entitiesInContext.push_back(wep.eids_[i]);
    }
  }
  // Deal with the last context
  size_t nofPossibilities =
      static_cast<size_t>(pow(entitiesInContext.size(), nofVars));
  for (size_t j = 0; j < nofPossibilities; ++j) {
    vector<Id> key;
    key.reserve(nofVars);
    size_t n = j;
    for (size_t k = 0; k < nofVars; ++k) {
      key.push_back(entitiesInContext[n % entitiesInContext.size()]);
      n /= entitiesInContext.size();
    }
    mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
    if (!mapEtSaC.contains(key)) {
      mapEtSaC[key].second = std::make_pair(cscore, currentCid);
      mapEtSaC[key].first = 1;
    } else {
      if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
        continue;
      }
      mapEtSaC[key].first++;
      ScoreAndContext& sac = mapEtSaC[key].second;
      if (sac.first < cscore) {
        sac = std::make_pair(cscore, currentCid);
      }
    }
  }
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaC.size() + 2);
  for (auto it = mapEtSaC.begin(); it != mapEtSaC.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const ScoreAndContext& sac = it->second.second;
    const Id entityScore = Id::makeFromInt(it->second.first);
    row[0] = Id::makeFromTextRecordIndex(sac.second);
    row[1] = entityScore;
    for (size_t k = 0; k < nofVars; ++k) {
      row[k + 2] = it->first[k];  // eid
    }
    const vector<vector<WordIndex>> widsVec =
        mapEaCtW[std::make_pair(it->first, sac.second)];
    for (size_t j = 0; j < widsVec.size(); j++) {
      for (size_t l = 0; l < numOfTerms; l++) {
        row[2 + nofVars + l] =
            Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
      }
      result.push_back(row);
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
}

template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<0>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<1>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<2>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<3>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<4>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<5>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<6>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<7>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<8>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<9>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);
template void FTSAlgorithms::multVarsAggScoresAndTakeTopContext<10>(
    const Index::WordEntityPostings& wep, size_t nofVars, IdTable* dynResult);

// _____________________________________________________________________________
void FTSAlgorithms::appendCrossProduct(const Index::WordEntityPostings& wep,
                                       size_t from, size_t toExclusive,
                                       const ad_utility::HashSet<Id>& subRes1,
                                       const ad_utility::HashSet<Id>& subRes2,
                                       vector<array<Id, 5>>& res) {
  LOG(TRACE) << "Append cross-product called for a context with "
             << toExclusive - from << " postings.\n";
  vector<Id> contextSubRes1;
  vector<Id> contextSubRes2;
  ad_utility::HashSet<Id> done;
  for (size_t i = from; i < toExclusive; ++i) {
    if (done.contains(wep.eids_[i])) {
      continue;
    }
    done.insert(wep.eids_[i]);
    if (subRes1.contains(wep.eids_[i])) {
      contextSubRes1.push_back(wep.eids_[i]);
    }
    if (subRes2.contains(wep.eids_[i])) {
      contextSubRes2.push_back(wep.eids_[i]);
    }
  }
  for (size_t i = from; i < toExclusive; ++i) {
    for (size_t j = 0; j < contextSubRes1.size(); ++j) {
      for (size_t k = 0; k < contextSubRes2.size(); ++k) {
        res.emplace_back(
            array<Id, 5>{{wep.eids_[i], Id::makeFromInt(wep.scores_[i]),
                          Id::makeFromTextRecordIndex(wep.cids_[i]),
                          contextSubRes1[j], contextSubRes2[k]}});
      }
    }
  }
}

// _____________________________________________________________________________
void FTSAlgorithms::appendCrossProduct(
    const Index::WordEntityPostings& wep, size_t from, size_t toExclusive,
    const vector<ad_utility::HashMap<Id, vector<vector<Id>>>>& subResMaps,
    vector<vector<Id>>& res) {
  vector<vector<vector<Id>>> subResMatches;
  subResMatches.resize(subResMaps.size());
  ad_utility::HashSet<Id> distinctEids;
  for (size_t i = from; i < toExclusive; ++i) {
    if (distinctEids.contains(wep.eids_[i])) {
      continue;
    }
    distinctEids.insert(wep.eids_[i]);
    for (size_t j = 0; j < subResMaps.size(); ++j) {
      if (subResMaps[j].contains(wep.eids_[i])) {
        for (const vector<Id>& row : subResMaps[j].find(wep.eids_[i])->second) {
          subResMatches[j].push_back(row);
        }
      }
    }
  }
  for (size_t i = from; i < toExclusive; ++i) {
    // In order to create the cross product between subsets,
    // we compute the number of result rows and use
    // modulo operations to index the correct sources.

    // Example: cross product between sets of sizes a x b x c
    // Then the n'th row is composed of:
    // n % a               from a,
    // (n / a) % b         from b,
    // ((n / a) / b) % c   from c.
    size_t nofResultRows = 1;
    for (size_t j = 0; j < subResMatches.size(); ++j) {
      nofResultRows *= subResMatches[j].size();
    }

    for (size_t n = 0; n < nofResultRows; ++n) {
      vector<Id> resRow = {wep.eids_[i], Id::makeFromInt(wep.scores_[i]),
                           Id::makeFromTextRecordIndex(wep.cids_[i])};
      for (size_t j = 0; j < subResMatches.size(); ++j) {
        size_t index = n;
        for (size_t k = 0; k < j; ++k) {
          index /= subResMatches[k].size();
        }
        index %= subResMatches[j].size();
        vector<Id>& append = subResMatches[j][index];
        resRow.insert(resRow.end(), append.begin(), append.end());
      }
      res.push_back(resRow);
    }
  }
}

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k,
    IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.eids_.size());
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.scores_.size());
  LOG(DEBUG)
      << "Going from a WordEntityPostings-Element consisting of an entity,"
      << " context, word and score list of size: " << wep.cids_.size()
      << " elements to a table with filtered distinct entities "
      << "and at most " << k << " contexts per entity.\n";
  if (wep.cids_.empty() || fMap.empty()) {
    return;
  }

  size_t numOfTerms = wep.wids_.size();

  // TODO: add code to speed up for k==1

  // Use a set (ordered) and keep it at size k for the context scores
  // This achieves O(n log k)
  LOG(DEBUG) << "Heap-using case with " << k << " contexts per entity...\n";

  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<Id, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
  using AggMapEntityToSaCS =
      ad_utility::HashMap<Id, std::pair<Score, ScoreAndContextSet>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaCS mapEtSaCS;
  for (size_t i = 0; i < wep.eids_.size(); ++i) {
    vector<WordIndex> wids;
    wids.resize(numOfTerms);
    for (size_t l = 0; l < numOfTerms; l++) {
      wids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
    }
    if (fMap.contains(wep.eids_[i])) {
      mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].emplace_back(wids);
      if (!mapEtSaCS.contains(wep.eids_[i])) {
        mapEtSaCS[wep.eids_[i]].second.emplace(wep.scores_[i], wep.cids_[i]);
        mapEtSaCS[wep.eids_[i]].first = 1;
      } else {
        if (mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].size() >= 2) {
          continue;
        }
        mapEtSaCS[wep.eids_[i]].first++;
        ScoreAndContextSet& sacs = mapEtSaCS[wep.eids_[i]].second;
        if (sacs.size() < k || sacs.begin()->first < wep.scores_[i]) {
          if (sacs.size() == k) {
            sacs.erase(*sacs.begin());
          }
          sacs.emplace(wep.scores_[i], wep.cids_[i]);
        }
      }
    }
  }
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaCS.size() + 2);
  for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const Id eid = it->first;
    const Id entityScore = Id::makeFromInt(it->second.first);
    const ScoreAndContextSet& sacs = it->second.second;
    row[1] = entityScore;
    for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
      const TextRecordIndex cid = itt->second;
      row[0] = Id::makeFromTextRecordIndex(cid);
      for (auto fRow : fMap.find(eid)->second) {
        size_t i;
        for (i = 0; i < fRow.numColumns(); i++) {
          row[2 + i] = fRow[i];
        }
        const vector<vector<WordIndex>> widsVec =
            mapEaCtW[std::make_pair(eid, cid)];
        for (size_t j = 0; j < widsVec.size(); j++) {
          for (size_t l = 0; l < numOfTerms; l++) {
            row[2 + i + l] =
                Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
          }
          result.push_back(row);
        }
      }
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
};

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t k, IdTable* dynResult);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.eids_.size());
  AD_CONTRACT_CHECK(wep.cids_.size() == wep.scores_.size());
  LOG(DEBUG) << "Going from an entity, context and score list of size: "
             << wep.cids_.size()
             << " elements to a table with filtered distinct entities "
             << "and at most " << k << " contexts per entity.\n";

  if (wep.cids_.empty() || fSet.empty()) {
    return;
  }

  size_t numOfTerms = wep.wids_.size();

  // TODO: add code to speed up for k==1

  // Use a set (ordered) and keep it at size k for the context scores
  // This achieves O(n log k)
  LOG(DEBUG) << "Heap-using case with " << k << " contexts per entity...\n";

  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<Id, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
  using AggMapEntityToSaCS =
      ad_utility::HashMap<Id, std::pair<Score, ScoreAndContextSet>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaCS mapEtSaCS;
  for (size_t i = 0; i < wep.eids_.size(); ++i) {
    vector<WordIndex> wids;
    wids.resize(numOfTerms);
    for (size_t l = 0; l < numOfTerms; l++) {
      wids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
    }
    if (fSet.contains(wep.eids_[i])) {
      mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].emplace_back(wids);
      if (!mapEtSaCS.contains(wep.eids_[i])) {
        mapEtSaCS[wep.eids_[i]].second.emplace(wep.scores_[i], wep.cids_[i]);
        mapEtSaCS[wep.eids_[i]].first = 1;
      } else {
        if (mapEaCtW[std::make_pair(wep.eids_[i], wep.cids_[i])].size() >= 2) {
          continue;
        }
        mapEtSaCS[wep.eids_[i]].first++;
        ScoreAndContextSet& sacs = mapEtSaCS[wep.eids_[i]].second;
        if (sacs.size() < k || sacs.begin()->first < wep.scores_[i]) {
          if (sacs.size() == k) {
            sacs.erase(*sacs.begin());
          }
          sacs.emplace(wep.scores_[i], wep.cids_[i]);
        }
      }
    }
  }
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaCS.size() + 2);
  for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const Id eid = it->first;
    const Id entityScore = Id::makeFromInt(it->second.first);
    const ScoreAndContextSet& sacs = it->second.second;
    row[1] = entityScore;
    row[2] = eid;
    for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
      row[0] = Id::makeFromTextRecordIndex(itt->second);
      const vector<vector<WordIndex>> widsVec =
          mapEaCtW[std::make_pair(eid, itt->second)];
      for (size_t j = 0; j < widsVec.size(); j++) {
        for (size_t l = 0; l < numOfTerms; l++) {
          row[3 + l] =
              Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
        }
        result.push_back(row);
      }
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
};

template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);
template void FTSAlgorithms::oneVarFilterAggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet, size_t k,
    IdTable* dynResult);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  if (wep.cids_.empty() || fMap.empty()) {
    return;
  }

  size_t numOfTerms = wep.wids_.size();

  // Go over contexts.
  // For each context build a cross product with one part being from the filter.
  // Store them in a map, use a pair of id's as key and
  // an appropriate hash function.
  // Use a set (ordered) and keep it at size kLimit for the context scores.
  LOG(DEBUG) << "Heap-using case with " << kLimit
             << " contexts per entity...\n";
  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<vector<Id>, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
  using AggMapEntityToSaCS =
      ad_utility::HashMap<vector<Id>, std::pair<Score, ScoreAndContextSet>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaCS mapEtSaCS;
  vector<Id> entitiesInContext;
  vector<Id> filteredEntitiesInContext;
  TextRecordIndex currentCid = wep.cids_[0];
  Score cscore = wep.scores_[0];
  vector<WordIndex> cwids;
  cwids.resize(numOfTerms);
  for (size_t l = 0; l < numOfTerms; l++) {
    cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][0];
  }

  for (size_t i = 0; i < wep.cids_.size(); ++i) {
    if (wep.cids_[i] == currentCid) {
      if (fMap.contains(wep.eids_[i])) {
        filteredEntitiesInContext.push_back(wep.eids_[i]);
      }
      entitiesInContext.push_back(wep.eids_[i]);
      // cscore += scores[i];
    } else {
      if (!filteredEntitiesInContext.empty()) {
        // Calculate a cross product and add/update the map
        size_t nofPossibilities =
            filteredEntitiesInContext.size() *
            static_cast<size_t>(pow(entitiesInContext.size(), nofVars - 1));
        for (size_t j = 0; j < nofPossibilities; ++j) {
          vector<Id> key;
          key.reserve(nofVars);
          size_t n = j;
          key.push_back(
              filteredEntitiesInContext[n % filteredEntitiesInContext.size()]);
          n /= filteredEntitiesInContext.size();
          for (size_t k = 1; k < nofVars; ++k) {
            key.push_back(entitiesInContext[n % entitiesInContext.size()]);
            n /= entitiesInContext.size();
          }
          mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
          if (!mapEtSaCS.contains(key)) {
            mapEtSaCS[key].second.emplace(cscore, currentCid);
            mapEtSaCS[key].first = 1;
          } else {
            if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
              continue;
            }
            mapEtSaCS[key].first++;
            ScoreAndContextSet& sacs = mapEtSaCS[key].second;
            if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
              if (sacs.size() == kLimit) {
                sacs.erase(*sacs.begin());
              }
              sacs.emplace(cscore, currentCid);
            }
          }
        }
      }
      entitiesInContext.clear();
      filteredEntitiesInContext.clear();
      currentCid = wep.cids_[i];
      cscore = wep.scores_[i];
      for (size_t l = 0; l < numOfTerms; l++) {
        cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
      }
      entitiesInContext.push_back(wep.eids_[i]);
      if (fMap.contains(wep.eids_[i])) {
        filteredEntitiesInContext.push_back(wep.eids_[i]);
      }
    }
  }
  // Deal with the last context
  if (!filteredEntitiesInContext.empty()) {
    // Calculate a cross product and add/update the map
    size_t nofPossibilities =
        filteredEntitiesInContext.size() *
        static_cast<size_t>(pow(entitiesInContext.size(), nofVars - 1));
    for (size_t j = 0; j < nofPossibilities; ++j) {
      vector<Id> key;
      key.reserve(nofVars);
      size_t n = j;
      key.push_back(
          filteredEntitiesInContext[n % filteredEntitiesInContext.size()]);
      n /= filteredEntitiesInContext.size();
      for (size_t k = 1; k < nofVars; ++k) {
        key.push_back(entitiesInContext[n % entitiesInContext.size()]);
        n /= entitiesInContext.size();
      }
      mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
      if (!mapEtSaCS.contains(key)) {
        mapEtSaCS[key].second.emplace(cscore, currentCid);
        mapEtSaCS[key].first = 1;
      } else {
        if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
          continue;
        }
        mapEtSaCS[key].first++;
        ScoreAndContextSet& sacs = mapEtSaCS[key].second;
        if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
          if (sacs.size() == kLimit) {
            sacs.erase(*sacs.begin());
          }
          sacs.emplace(cscore, currentCid);
        }
      }
    }
  }

  // Iterate over the map and populate the result.
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaCS.size() + 2);
  for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const Id entityScore = Id::makeFromInt(it->second.first);
    const vector<Id>& keyEids = it->first;
    const IdTable& filterRows = fMap.find(keyEids[0])->second;
    const ScoreAndContextSet& sacs = it->second.second;

    for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
      for (auto fRow : filterRows) {
        const TextRecordIndex cid = itt->second;
        const vector<vector<WordIndex>> widsVec =
            mapEaCtW[std::make_pair(it->first, cid)];
        size_t off = 2;
        row[0] = Id::makeFromTextRecordIndex(cid);
        row[1] = entityScore;
        for (size_t i = 1; i < keyEids.size(); i++) {
          row[off] = keyEids[i];
          off++;
        }
        for (size_t i = 0; i < fRow.numColumns(); i++) {
          row[off] = fRow[i];
          off++;
        }
        for (size_t j = 0; j < widsVec.size(); j++) {
          for (size_t l = 0; l < numOfTerms; l++) {
            row[2 + off + l] =
                Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
          }
        }
        result.push_back(row);
      }
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
}

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep,
    const ad_utility::HashMap<Id, IdTable>& fMap, size_t nofVars, size_t kLimit,
    IdTable* dynResult);

// _____________________________________________________________________________
template <int WIDTH>
void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult) {
  AD_CONTRACT_CHECK(wep.wids_.size() >= 1);
  if (wep.cids_.empty() || fSet.empty()) {
    return;
  }
  size_t numOfTerms = wep.wids_.size();
  // Go over contexts.
  // For each context build a cross product with one part being from the filter.
  // Store them in a map, use a pair of id's as key and
  // an appropriate hash function.
  // Use a set (ordered) and keep it at size kLimit for the context scores.
  LOG(DEBUG) << "Heap-using case with " << kLimit
             << " contexts per entity...\n";
  using AggMapContextAndEntityToWord =
      ad_utility::HashMap<std::pair<vector<Id>, TextRecordIndex>,
                          vector<vector<WordIndex>>>;
  using ScoreAndContextSet = std::set<std::pair<Score, TextRecordIndex>>;
  using AggMapEntityToSaCS =
      ad_utility::HashMap<vector<Id>, std::pair<Score, ScoreAndContextSet>>;
  AggMapContextAndEntityToWord mapEaCtW;
  AggMapEntityToSaCS mapEtSaCS;
  vector<Id> entitiesInContext;
  vector<Id> filteredEntitiesInContext;
  TextRecordIndex currentCid = wep.cids_[0];
  Score cscore = wep.scores_[0];
  vector<WordIndex> cwids;
  cwids.resize(numOfTerms);
  for (size_t l = 0; l < numOfTerms; l++) {
    cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][0];
  }

  for (size_t i = 0; i < wep.cids_.size(); ++i) {
    if (wep.cids_[i] == currentCid) {
      if (fSet.contains(wep.eids_[i])) {
        filteredEntitiesInContext.push_back(wep.eids_[i]);
      }
      entitiesInContext.push_back(wep.eids_[i]);
      // cscore += scores[i];
    } else {
      if (!filteredEntitiesInContext.empty()) {
        // Calculate a cross product and add/update the map
        size_t nofPossibilities =
            filteredEntitiesInContext.size() *
            static_cast<size_t>(pow(entitiesInContext.size(), nofVars - 1));
        for (size_t j = 0; j < nofPossibilities; ++j) {
          vector<Id> key;
          key.reserve(nofVars);
          size_t n = j;
          key.push_back(
              filteredEntitiesInContext[n % filteredEntitiesInContext.size()]);
          n /= filteredEntitiesInContext.size();
          for (size_t k = 1; k < nofVars; ++k) {
            key.push_back(entitiesInContext[n % entitiesInContext.size()]);
            n /= entitiesInContext.size();
          }
          mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
          if (!mapEtSaCS.contains(key)) {
            mapEtSaCS[key].second.emplace(cscore, currentCid);
            mapEtSaCS[key].first = 1;
          } else {
            if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
              continue;
            }
            mapEtSaCS[key].first++;
            ScoreAndContextSet& sacs = mapEtSaCS[key].second;
            if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
              if (sacs.size() == kLimit) {
                sacs.erase(*sacs.begin());
              }
              sacs.emplace(cscore, currentCid);
            }
          }
        }
      }
      entitiesInContext.clear();
      filteredEntitiesInContext.clear();
      currentCid = wep.cids_[i];
      cscore = wep.scores_[i];
      for (size_t l = 0; l < numOfTerms; l++) {
        cwids[l] = wep.wids_[l].empty() ? 0 : wep.wids_[l][i];
      }
      entitiesInContext.push_back(wep.eids_[i]);
      if (fSet.contains(wep.eids_[i])) {
        filteredEntitiesInContext.push_back(wep.eids_[i]);
      }
    }
  }
  // Deal with the last context
  if (!filteredEntitiesInContext.empty()) {
    // Calculate a cross product and add/update the map
    size_t nofPossibilities =
        filteredEntitiesInContext.size() *
        static_cast<size_t>(pow(entitiesInContext.size(), nofVars - 1));
    for (size_t j = 0; j < nofPossibilities; ++j) {
      vector<Id> key;
      key.reserve(nofVars);
      size_t n = j;
      key.push_back(
          filteredEntitiesInContext[n % filteredEntitiesInContext.size()]);
      n /= filteredEntitiesInContext.size();
      for (size_t k = 1; k < nofVars; ++k) {
        key.push_back(entitiesInContext[n % entitiesInContext.size()]);
        n /= entitiesInContext.size();
      }
      mapEaCtW[std::make_pair(key, currentCid)].emplace_back(cwids);
      if (!mapEtSaCS.contains(key)) {
        mapEtSaCS[key].second.emplace(cscore, currentCid);
        mapEtSaCS[key].first = 1;
      } else {
        if (mapEaCtW[std::make_pair(key, currentCid)].size() >= 2) {
          continue;
        }
        mapEtSaCS[key].first++;
        ScoreAndContextSet& sacs = mapEtSaCS[key].second;
        if (sacs.size() < kLimit || sacs.begin()->first < cscore) {
          if (sacs.size() == kLimit) {
            sacs.erase(*sacs.begin());
          }
          sacs.emplace(cscore, currentCid);
        }
      }
    }
  }

  // Iterate over the map and populate the result.
  IdTableStatic<WIDTH> result = std::move(*dynResult).toStatic<WIDTH>();
  result.reserve(mapEtSaCS.size() + 2);
  for (auto it = mapEtSaCS.begin(); it != mapEtSaCS.end(); ++it) {
    std::vector<ValueId> row;
    row.resize(result.numColumns());
    const ScoreAndContextSet& sacs = it->second.second;
    row[1] = Id::makeFromInt(it->second.first);
    const vector<Id>& keyEids = it->first;
    size_t off = 2;
    for (size_t i = 1; i < keyEids.size(); i++) {
      row[2 + i] = keyEids[i];
      off++;
    }
    row[off] = keyEids[0];
    for (auto itt = sacs.rbegin(); itt != sacs.rend(); ++itt) {
      const TextRecordIndex cid = itt->second;
      row[0] = Id::makeFromTextRecordIndex(cid);
      const vector<vector<WordIndex>> widsVec =
          mapEaCtW[std::make_pair(it->first, cid)];
      for (size_t j = 0; j < widsVec.size(); j++) {
        for (size_t l = 0; l < numOfTerms; l++) {
          row[2 + off + 1 + l] =
              Id::makeFromWordVocabIndex(WordVocabIndex::make(widsVec[j][l]));
        }
        result.push_back(row);
      }
    }
  }
  *dynResult = std::move(result).toDynamic();
  LOG(DEBUG) << "Done. There are " << dynResult->size() << " tuples now.\n";
}

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<0>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<1>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<2>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<3>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<4>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);

template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<5>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<6>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<7>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<8>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<9>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
template void FTSAlgorithms::multVarsFilterAggScoresAndTakeTopKContexts<10>(
    const Index::WordEntityPostings& wep, const HashSet<Id>& fSet,
    size_t nofVars, size_t kLimit, IdTable* dynResult);
