#include <gtest/gtest.h>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "../src/global/Constants.h"
#include "../src/index/ConstantsIndexCreation.h"
#include "../src/index/VocabularyGenerator.h"

// equality operator used in this test
bool vocabTestCompare(const IdPairMMapVecView& a,
                      const std::vector<std::pair<Id, Id>>& b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }

  return true;
}

// Test fixture that sets up the binary files vor partial vocabulary and
// everything else connected with vocabulary merging.
class MergeVocabularyTest : public ::testing::Test {
 protected:
  // path of the 2 partial Vocabularies that are used by mergeVocabulary
  std::string _path0;
  std::string _path1;
  // path of the 2 partial vocabularies that are the expected output of
  // mergeVocabulary
  std::string _pathExp0;
  std::string _pathExp1;
  // the base directory for our test
  std::string _basePath;
  // path to expected vocabulary text file
  std::string _pathVocabExp;
  // path to expected external vocabulary text file
  std::string _pathExternalVocabExp;

  // two std::vectors where we store the expected mapping
  // form partial to global ids;
  std::vector<std::pair<Id, Id>> _expMapping0;
  std::vector<std::pair<Id, Id>> _expMapping1;

  // Constructor. TODO: Better write Setup method because of complex logic which
  // may throw?
  MergeVocabularyTest() {
    // name of random subdirectory
    std::srand(std::time(nullptr));
    _basePath = std::string("qleverVocTest-") + std::to_string(std::rand());
    // those names are required by mergeVocabulary
    _path0 = std::string(PARTIAL_VOCAB_FILE_NAME + std::to_string(0));
    _path1 = std::string(PARTIAL_VOCAB_FILE_NAME + std::to_string(1));
    // those names can be random
    _pathExp0 = std::string(".partial-vocabulary-expected0");
    _pathExp1 = std::string(".partial-vocabulary-expected1");

    // create random subdirectory in /tmp
    std::string tempPath = "/tmp/";
    _basePath = tempPath + _basePath + "/";
    if (system(("mkdir -p " + _basePath).c_str())) {
      // system should return 0 on success
      std::cerr << "Could not create subfolder of tmp for test. this might "
                   "lead to test failures\n";
    }

    // make paths abolute under created tmp directory
    _path0 = _basePath + _path0;
    _path1 = _basePath + _path1;
    _pathExp0 = _basePath + _pathExp0;
    _pathExp1 = _basePath + _pathExp1;
    _pathVocabExp = _basePath + std::string(".vocabExp");
    _pathExternalVocabExp = _basePath + std::string("externalTextFileExp");

    // these will be the contents of partial vocabularies, second element of
    // pair is the correct Id which is expected from mergeVocabulary
    std::vector<std::pair<std::string, size_t>> words1{
        {"ape", 0},
        {"gorilla", 2},
        {"monkey", 3},
        {std::string{EXTERNALIZED_LITERALS_PREFIX} + "bla", 5}};
    std::vector<std::pair<std::string, size_t>> words2{
        {"bear", 1}, {"monkey", 3}, {"zebra", 4}};

    // write expected vocabulary files
    std::ofstream expVoc(_pathVocabExp);
    std::ofstream expExtVoc(_pathExternalVocabExp);
    expVoc << "ape\nbear\ngorilla\nmonkey\nzebra\n";
    expExtVoc << "bla\n";

    // open files for partial Vocabularies
    auto mode = std::ios_base::out | std::ios_base::binary;
    std::ofstream partial0(_path0, mode);
    std::ofstream partial1(_path1, mode);
    std::ofstream partialExp0(_pathExp0, mode);
    std::ofstream partialExp1(_pathExp1, mode);

    if (!partial0.is_open())
      std::cerr << "could not open temp file at" << _path0 << '\n';
    if (!partial1.is_open())
      std::cerr << "could not open temp file at" << _path1 << '\n';
    if (!partialExp0.is_open())
      std::cerr << "could not open temp file at" << _pathExp0 << '\n';
    if (!partialExp1.is_open())
      std::cerr << "could not open temp file at" << _pathExp1 << '\n';

    // write first partial vocabulary
    size_t localIdx = 0;
    for (const auto& w : words1) {
      std::string word = w.first;
      uint32_t len = word.size();
      // write 4 Bytes of string length
      partial0.write((char*)&len, sizeof(uint32_t));
      partialExp0.write((char*)&len, sizeof(uint32_t));

      // write the word
      partial0.write(word.c_str(), len);
      partialExp0.write(word.c_str(), len);

      // these indices are in order and are not supposed to change ever
      partial0.write((char*)&localIdx, sizeof(size_t));
      partialExp0.write((char*)&localIdx, sizeof(size_t));
      _expMapping0.emplace_back(localIdx, w.second);
      localIdx++;
    }

    // write second partialVocabulary
    localIdx = 0;
    for (const auto& w : words2) {
      std::string word = w.first;
      uint32_t len = word.size();
      partial1.write((char*)&len, sizeof(uint32_t));
      partialExp1.write((char*)&len, sizeof(uint32_t));

      partial1.write(word.c_str(), len);
      partialExp1.write(word.c_str(), len);

      partial1.write((char*)&localIdx, sizeof(size_t));
      partialExp1.write((char*)&localIdx, sizeof(size_t));
      _expMapping1.emplace_back(localIdx, w.second);
      localIdx++;
    }
  }

  // __________________________________________________________________
  ~MergeVocabularyTest() {
    // TODO: shall we delete the tmp files? doing so is cleaner, but makes it
    // harder to debug test failures
  }

  // returns true if and only if the files with names n1 and n2 exist, can be
  // opened for reading and are bytewise equal.
  bool areBinaryFilesEqual(const std::string& n1, const std::string& n2) {
    auto p1 = readAllBytes(n1);
    auto p2 = readAllBytes(n2);
    auto s1 = p1.first;
    auto s2 = p2.first;
    auto& f1 = p1.second;
    auto& f2 = p2.second;

    return (s1 && s2 && f1.size() == f2.size() &&
            std::equal(f1.begin(), f1.end(), f2.begin()));
  }

  // read all bytes from a file (e.g. to check equality of small test files)
  static std::pair<bool, std::vector<char>> readAllBytes(
      const std::string& filename) {
    using std::ifstream;
    ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
      return std::make_pair(false, std::vector<char>());
    }
    ifstream::pos_type pos = ifs.tellg();

    std::vector<char> result(pos);

    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);

    return std::make_pair(true, result);
  }
};

// Test for merge Vocabulary
TEST_F(MergeVocabularyTest, bla) {
  // mergeVocabulary only gets name of directory and number of files.
  Id langPredLowerBound, langPredUpperBound;
  mergeVocabulary(_basePath, 2, &langPredLowerBound, &langPredUpperBound);
  // No language tags in text file
  ASSERT_EQ(langPredLowerBound, 0ul);
  ASSERT_EQ(langPredUpperBound, 0ul);
  // Assert that partial vocabularies have the expected ids
  ASSERT_TRUE(areBinaryFilesEqual(_path0, _pathExp0));
  ASSERT_TRUE(areBinaryFilesEqual(_path1, _pathExp1));
  // check that (external) vocabulary has the right form.
  ASSERT_TRUE(areBinaryFilesEqual(_pathVocabExp, _basePath + ".vocabulary"));
  ASSERT_TRUE(areBinaryFilesEqual(_pathExternalVocabExp,
                                  _basePath + EXTERNAL_LITS_TEXT_FILE_NAME));

  IdPairMMapVecView mapping0(_basePath + PARTIAL_MMAP_IDS + std::to_string(0));
  ASSERT_TRUE(vocabTestCompare(mapping0, _expMapping0));
  IdPairMMapVecView mapping1(_basePath + PARTIAL_MMAP_IDS + std::to_string(1));
  ASSERT_TRUE(vocabTestCompare(mapping1, _expMapping1));
}
