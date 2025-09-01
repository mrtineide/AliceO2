// Copyright 2019-2025 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

///
/// \file   testCcdbApiHeaders.cxx
/// \brief  Test BasicCCDBManager header/metadata information functionality with caching
/// \author martin.oines.eide@cern.ch

#define BOOST_TEST_MODULE CCDB
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/tools/interface.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <boost/test/tools/interface.hpp>

#include "CCDB/BasicCCDBManager.h"
#include "CCDB/CCDBTimeStampUtils.h"
#include "CCDB/CcdbApi.h"

static std::string basePath;
// std::string ccdbUrl = "http://localhost:8080";
std::string ccdbUrl = "http://ccdb-test.cern.ch:8080";
bool hostReachable = false;

/**
 * Global fixture, ie general setup and teardown
 * Copied from testBasicCCDBManager.cxx
 */
struct Fixture {
  Fixture()
  {
    o2::ccdb::CcdbApi api;
    if (std::getenv("ALICEO2_CCDB_HOST"))
      ccdbUrl = std::string(std::getenv("ALICEO2_CCDB_HOST"));
    api.init(ccdbUrl);
    hostReachable = api.isHostReachable();
    char hostname[_POSIX_HOST_NAME_MAX];
    gethostname(hostname, _POSIX_HOST_NAME_MAX);
    basePath = std::string("Users/m/meide/BasicCCDBManager/");

    LOG(info) << "Path we will use in this test suite : " + basePath << std::endl;
    LOG(info) << "ccdb url: " << ccdbUrl << std::endl;
    LOG(info) << "Is host reachable ? --> " << hostReachable << std::endl;
  }
  ~Fixture()
  {
    if (hostReachable) {
      o2::ccdb::CcdbApi api;
      api.init(ccdbUrl);
      api.truncate(basePath + "*"); // This deletes the data after test is run, disable if you want to inspect the data
      LOG(info) << "Test data truncated/deleted (" << basePath << ")" << std::endl;
    }
  }
};
BOOST_GLOBAL_FIXTURE(Fixture);
/**
 * Just an accessor to the hostReachable variable to be used to determine whether tests can be ran or not.
 * Copied from testCcdbApi.cxx
 */
struct if_reachable {
  boost::test_tools::assertion_result operator()(boost::unit_test::test_unit_id)
  {
    return hostReachable;
  }
};
/**
 * Fixture for the tests, i.e. code is ran in every test that uses it, i.e. it is like a setup and teardown for tests.
 * Copied from testCcdbApi.cxx
 */
struct test_fixture {
  test_fixture()
  {
    api.init(ccdbUrl);
    metadata["Hello"] = "World";
    LOG(info) << "*** " << boost::unit_test::framework::current_test_case().p_name << " ***" << std::endl;
  }
  ~test_fixture() = default;

  o2::ccdb::CcdbApi api;
  std::map<std::string, std::string> metadata;
};

// Only compare known and stable keys (avoid volatile ones like Date)
static const std::set<std::string> sStableKeys = {
  "ETag",
  "Valid-From",
  "Valid-Until",
  "Created",
  "Last-Modified",
  "Content-Disposition",
  "Content-Location",
  "path",
  "partName",
  "Content-MD5",
  "Hello" // TODO find other headers to compare to
};

// Test that we get back the same header header keys as we put in (for stable keys)

BOOST_AUTO_TEST_CASE(testCachedHeaders, *boost::unit_test::precondition(if_reachable()))
{
  /// ━━━━━━━ ARRANGE ━━━━━━━━━
  // First store objects to test with
  test_fixture f;

  std::string pathA = basePath + "CachingA";
  std::string pathB = basePath + "CachingB";
  std::string pathC = basePath + "CachingC";
  std::string ccdbObjO = "testObjectO";
  std::string ccdbObjN = "testObjectN";
  std::string ccdbObjX = "testObjectX";
  std::map<std::string, std::string> md = f.metadata;
  long start = 1000, stop = 3000;
  f.api.storeAsTFileAny<std::string>(&ccdbObjO, pathA, md, start, stop);
  f.api.storeAsTFileAny<std::string>(&ccdbObjN, pathB, md, start, stop);
  f.api.storeAsTFileAny<std::string>(&ccdbObjX, pathC, md, start, stop);
  // initilize the BasicCCDBManager
  o2::ccdb::BasicCCDBManager& ccdbManager = o2::ccdb::BasicCCDBManager::instance();
  ccdbManager.clearCache();
  ccdbManager.setURL(ccdbUrl);
  ccdbManager.setCaching(true); // This is what we want to test.

  /// ━━━━━━━━━━━ ACT ━━━━━━━━━━━━
  // Plan: get one object, then another, then the first again and check the headers are the same
  std::map<std::string, std::string> headers1, headers2, headers3;

  auto* obj1 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers1);
  auto* obj2 = ccdbManager.getForTimeStamp<std::string>(pathB, (start + stop) / 2, &headers2);
  auto* obj3 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers3); // Should lead to a cache hit!

  /// ━━━━━━━━━━━ ASSERT ━━━━━━━━━━━━
  /// Check that we got something
  BOOST_REQUIRE(obj1 != nullptr);
  BOOST_REQUIRE(obj2 != nullptr);
  BOOST_REQUIRE(obj3 != nullptr);

  LOG(debug) << "obj1: " << *obj1;
  LOG(debug) << "obj2: " << *obj2;
  LOG(debug) << "obj3: " << *obj3;

  // Sanity check
  /// Check that the objects are correct
  BOOST_TEST(*obj1 == ccdbObjO);
  BOOST_TEST(*obj3 == ccdbObjO);
  BOOST_TEST(obj3 == obj1); // should be the same object in memory since it is cached

  BOOST_TEST(obj2 != obj1);

  (*obj1) = "ModifiedObject";
  BOOST_TEST(*obj1 == "ModifiedObject");
  BOOST_TEST(*obj3 == "ModifiedObject"); // obj3 and obj1 are the same object in memory

  // Check that the headers are the same for the two retrievals of the same object
  BOOST_REQUIRE(headers1.size() != 0);
  BOOST_REQUIRE(headers3.size() != 0);

  LOG(debug) << "Headers1 size: " << headers1.size();
  for (const auto& h : headers1) {
    LOG(debug) << "  " << h.first << " -> " << h.second;
  }
  LOG(debug) << "Headers3 size: " << headers3.size();
  for (const auto& h : headers3) {
    LOG(debug) << "  " << h.first << " -> " << h.second;
  }

  for (const auto& stableKey : sStableKeys) {
    LOG(info) << "Checking key: " << stableKey;

    BOOST_REQUIRE(headers1.count(stableKey) > 0);
    BOOST_REQUIRE(headers3.count(stableKey) > 0);
    BOOST_TEST(headers1.at(stableKey) == headers3.at(stableKey));
  }
  BOOST_TEST(headers1 != headers2, "The headers for different objects should be different");

  // Test that we can  change the map and the two headers are not affected
  headers1["NewKey"] = "NewValue";
  headers3["NewKey"] = "DifferentValue";
  BOOST_TEST(headers1["NewKey"] != headers3["NewKey"]); // This tests that we have a deep copy of the headers
}

BOOST_AUTO_TEST_CASE(testNonCachedHeaders, *boost::unit_test::precondition(if_reachable()))
{
  /// ━━━━━━━ ARRANGE ━━━━━━━━━
  // First store objects to test with
  test_fixture f;

  std::string pathA = basePath + "NonCachingA";
  std::string pathB = basePath + "NonCachingB";
  std::string ccdbObjO = "testObjectO";
  std::string ccdbObjN = "testObjectN";
  std::map<std::string, std::string> md = f.metadata;
  long start = 1000, stop = 2000;
  f.api.storeAsTFileAny(&ccdbObjO, pathA, md, start, stop);
  f.api.storeAsTFileAny(&ccdbObjN, pathB, md, start, stop);
  // initilize the BasicCCDBManager
  o2::ccdb::BasicCCDBManager& ccdbManager = o2::ccdb::BasicCCDBManager::instance();
  ccdbManager.clearCache();
  ccdbManager.setURL(ccdbUrl);
  ccdbManager.setCaching(false); // This is what we want to test, no caching

  /// ━━━━━━━━━━━ ACT ━━━━━━━━━━━━
  // Plan: get one object, then another, then the first again. Then check that the contents is the same but not the object in memory
  std::map<std::string, std::string> headers1, headers2, headers3;

  auto* obj1 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers1);
  auto* obj2 = ccdbManager.getForTimeStamp<std::string>(pathB, (start + stop) / 2, &headers2);
  auto* obj3 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers3); // Should not be cached since explicitly disabled

  /// ━━━━━━━━━━━ ASSERT ━━━━━━━━━━━
  /// Check that we got something
  BOOST_REQUIRE(obj1 != nullptr);
  BOOST_REQUIRE(obj2 != nullptr);
  BOOST_REQUIRE(obj3 != nullptr);

  LOG(debug) << "obj1: " << *obj1;
  LOG(debug) << "obj2: " << *obj2;
  LOG(debug) << "obj3: " << *obj3;

  // Sanity check
  /// Check that the objects are correct
  BOOST_TEST(*obj1 == ccdbObjO);
  BOOST_TEST(*obj3 == ccdbObjO);
  BOOST_TEST(obj2 != obj1);
  BOOST_TEST(obj3 != obj1); // should NOT be the same object in memory
  (*obj1) = "ModifiedObject";
  BOOST_TEST(*obj1 == "ModifiedObject");
  BOOST_TEST(*obj3 != "ModifiedObject"); // obj3 and obj1 are NOT the same object in memory

  BOOST_TEST(headers1.size() == headers3.size());

  // Remove the date header since it may be different even for the same object since we might have asked in different seconds
  headers1.erase("Date");
  headers3.erase("Date");
  BOOST_TEST(headers1 == headers3, "The headers for the same object should be the same even if not cached");

  BOOST_TEST(headers1 != headers2, "The headers for different objects should be different");
  BOOST_TEST(headers1.size() != 0);
  BOOST_TEST(headers3.size() != 0);
  BOOST_TEST(headers2.size() != 0);
  BOOST_TEST(headers1 != headers2, "The headers for different objects should be different");
}

BOOST_AUTO_TEST_CASE(test_header_filtering, *boost::unit_test::precondition(if_reachable()))
{
  /// ━━━━━━━ ARRANGE ━━━━━━━━━
  // First store objects to test with
  test_fixture f;

  std::string pathA = basePath + "CachingA";
  std::string pathB = basePath + "CachingB";
  std::string ccdbObjO = "testObjectO";
  std::string ccdbObjN = "testObjectN";
  std::map<std::string, std::string> md = f.metadata;
  long start = 1000, stop = 3000;
  f.api.storeAsTFileAny<std::string>(&ccdbObjO, pathA, md, start, stop);
  f.api.storeAsTFileAny<std::string>(&ccdbObjN, pathB, md, start + 1, stop + 1);
  // initilize the BasicCCDBManager
  o2::ccdb::BasicCCDBManager& ccdbManager = o2::ccdb::BasicCCDBManager::instance();
  ccdbManager.clearCache();
  ccdbManager.setURL(ccdbUrl);
  ccdbManager.setCaching(true); // This is what we want to test.

  // ━━━━━━━━━━━━ ACT ━━━━━━━━━━━━
  // Plan: get the objects but also include a string view to indicate which headers to keep

  // Convert the stable keys to a vector of string views
  std::vector<std::string_view> headerFilter;
  for (const auto& k : sStableKeys) {
    headerFilter.push_back(k);
  }

  std::map<std::string, std::string> headers1, headers2, headers3;
  auto* obj1 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers1, headerFilter);
  auto* obj2 = ccdbManager.getForTimeStamp<std::string>(pathB, (start + stop) / 2, &headers2, headerFilter);
  auto* obj3 = ccdbManager.getForTimeStamp<std::string>(pathA, (start + stop) / 2, &headers3, headerFilter); // Should lead to a cache hit

  /// ━━━━━━━━━━━ ASSERT ━━━━━━━━━━━

  /// Check that we got something
  BOOST_REQUIRE(obj1 != nullptr);
  BOOST_REQUIRE(obj2 != nullptr);
  BOOST_REQUIRE(obj3 != nullptr);

  LOG(debug) << "obj1: " << *obj1;
  LOG(debug) << "obj2: " << *obj2;
  LOG(debug) << "obj3: " << *obj3;
  /// Sanity check
  // Check that the objects are correct
  BOOST_TEST(*obj1 == ccdbObjO);
  BOOST_TEST(*obj3 == ccdbObjO);
  BOOST_TEST(obj3 == obj1); // should be the same object in memory since it is cached
  BOOST_TEST(obj2 != obj1);

  // Modify one and check that the other is also modified
  (*obj1) = "ModifiedObject";
  BOOST_TEST(*obj1 == "ModifiedObject");
  BOOST_TEST(*obj3 == "ModifiedObject");

  BOOST_REQUIRE(headers1.size() != 0);
  BOOST_REQUIRE(headers3.size() != 0);

  LOG(debug) << "Headers1 size: " << headers1.size();
  for (const auto& h : headers1) {
    LOG(debug) << "  " << h.first << " -> " << h.second;
  }
  LOG(debug) << "Headers3 size: " << headers3.size();
  for (const auto& h : headers3) {
    LOG(debug) << "  " << h.first << " -> " << h.second;
  }

  /// Test the headers keys and values are the same for the two retrievals of the same object
  for (const auto& stableKey : sStableKeys) {
    LOG(debug) << "Checking header: " << stableKey;

    BOOST_TEST(headers1.count(stableKey) > 0, "stable key missing in headers1: " + stableKey);
    BOOST_TEST(headers3.count(stableKey) > 0, "stable key missing in headers3: " + stableKey);

    BOOST_TEST(headers1.at(stableKey) == headers3.at(stableKey));
  }

  // Now test that the filtering worked, i.e. that we only have the stable keys
  for (const auto& h : headers1) {
    BOOST_TEST(sStableKeys.count(h.first) > 0, "Found non-stable key in headers1: " + h.first);
  }
  for (const auto& h : headers2) {
    BOOST_TEST(sStableKeys.count(h.first) > 0, "Found non-stable key in headers2: " + h.first);
  }
  for (const auto& h : headers3) {
    BOOST_TEST(sStableKeys.count(h.first) > 0, "Found non-stable key in headers3: " + h.first);
  }

  // Now check that we just have the filtered keys
  BOOST_TEST(headers1.size() == sStableKeys.size());
  BOOST_TEST(headers3.size() == sStableKeys.size());

  // Extract the keys and values so that we can compare the sets
  std::set<std::string> header1Keys, header2Keys, header3Keys;
  std::set<std::string> header1Values, header2Values;

  for (const auto& h : headers1) {
    header1Keys.insert(h.first);
  }
  for (const auto& h : headers2) {
    header2Keys.insert(h.first);
  }
  for (const auto& h : headers3) {
    header3Keys.insert(h.first);
  }

  BOOST_TEST(header1Keys.size() == sStableKeys.size());
  BOOST_TEST(header2Keys.size() == sStableKeys.size());
  BOOST_TEST(header3Keys.size() == sStableKeys.size());

  BOOST_TEST(header1Keys == header2Keys, boost::test_tools::per_element());
  BOOST_TEST(header2Keys == header3Keys, boost::test_tools::per_element());
  BOOST_TEST(header1Keys == header3Keys, boost::test_tools::per_element());

  BOOST_TEST(header1Keys == sStableKeys, boost::test_tools::per_element());
  BOOST_TEST(header2Keys == sStableKeys, boost::test_tools::per_element());
  BOOST_TEST(header3Keys == sStableKeys, boost::test_tools::per_element());

  /// Make sure that headers for different objects are different
  BOOST_TEST(headers1 != headers2, "The headers for different objects should be different (mainly ETag)");

  // Make a set of keys that we know beforehand is the same for different objects
  std::set<std::string> keysThatShouldBeDifferent = {
    "Valid-From",
    "Valid-Until",
    "Created",
    "ETag",
    "Content-Disposition",
    "Content-Location",
    "path",
    "Content-MD5",
  };

  for (const auto& stableKey : keysThatShouldBeDifferent) {
    BOOST_TEST(headers2.count(stableKey) > 0, "stable key missing in headers2: " + stableKey);
    header2Values.insert(headers2.at(stableKey));
    header1Values.insert(headers1.at(stableKey));
  }

  BOOST_TEST(header2Values.size() == keysThatShouldBeDifferent.size());

  BOOST_TEST(header2Values != header1Values, boost::test_tools::per_element());
}
