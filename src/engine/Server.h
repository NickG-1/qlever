// Copyright 2021 - 2022, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Johannes Kalmbach<kalmbach@cs.uni-freiburg.de>
//          Hannah Bast <bast@cs.uni-freiburg.de>

#pragma once

#include <semaphore>
#include <string>
#include <vector>

#include "engine/Engine.h"
#include "engine/QueryExecutionContext.h"
#include "engine/QueryExecutionTree.h"
#include "engine/SortPerformanceEstimator.h"
#include "index/Index.h"
#include "parser/SparqlParser.h"
#include "util/AllocatorWithLimit.h"
#include "util/MemorySize/MemorySize.h"
#include "util/ParseException.h"
#include "util/http/HttpServer.h"
#include "util/http/streamable_body.h"
#include "util/http/websocket/QueryHub.h"
#include "util/json.h"

using nlohmann::json;
using std::string;
using std::vector;

//! The HTTP Server used.
class Server {
 public:
  explicit Server(unsigned short port, size_t numThreads,
                  ad_utility::MemorySize maxMem, std::string accessToken,
                  bool usePatternTrick = true);

  virtual ~Server() = default;

  using ParamValueMap = ad_utility::HashMap<string, string>;

 private:
  //! Initialize the server.
  void initialize(const string& indexBaseName, bool useText,
                  bool usePatterns = true, bool loadAllPermutations = true);

 public:
  //! First initialize the server. Then loop, wait for requests and trigger
  //! processing. This method never returns except when throwing an exception.
  void run(const string& indexBaseName, bool useText, bool usePatterns = true,
           bool loadAllPermutations = true);

  Index& index() { return index_; }
  const Index& index() const { return index_; }

  /// Helper struct bundling a parsed query with a query execution tree.
  struct PlannedQuery {
    ParsedQuery parsedQuery_;
    QueryExecutionTree queryExecutionTree_;
  };

 private:
  const size_t numThreads_;
  unsigned short port_;
  std::string accessToken_;
  QueryResultCache cache_;
  ad_utility::AllocatorWithLimit<Id> allocator_;
  SortPerformanceEstimator sortPerformanceEstimator_;
  Index index_;
  ad_utility::websocket::QueryRegistry queryRegistry_{};

  bool enablePatternTrick_;

  /// Non-owning reference to the `QueryHub` instance living inside
  /// the `WebSocketHandler` created for `HttpServer`.
  std::weak_ptr<ad_utility::websocket::QueryHub> queryHub_;

  mutable net::static_thread_pool threadPool_;

  template <typename T>
  using Awaitable = boost::asio::awaitable<T>;

  using TimeLimit = std::chrono::seconds;

  /// Parse the path and URL parameters from the given request. Supports both
  /// GET and POST request according to the SPARQL 1.1 standard.
  ad_utility::UrlParser::UrlPathAndParameters getUrlPathAndParameters(
      const ad_utility::httpUtils::HttpRequest auto& request);

  /// Handle a single HTTP request. Check whether a file request or a query was
  /// sent, and dispatch to functions handling these cases. This function
  /// requires the constraints for the `HttpHandler` in `HttpServer.h`.
  /// \param req The HTTP request.
  /// \param send The action that sends a http:response. (see the
  ///             `HttpServer.h` for documentation).
  Awaitable<void> process(
      const ad_utility::httpUtils::HttpRequest auto& request, auto&& send);

  /// Handle a http request that asks for the processing of a query.
  /// \param params The key-value-pairs  sent in the HTTP GET request. When this
  /// function is called, we already know that a parameter "query" is contained
  /// in `params`.
  /// \param requestTimer Timer that measure the total processing
  ///                     time of this request.
  /// \param request The HTTP request.
  /// \param send The action that sends a http:response (see the
  ///             `HttpServer.h` for documentation).
  /// \param timeLimit Duration in seconds after which the query will be
  ///                  cancelled.
  Awaitable<void> processQuery(
      const ParamValueMap& params, ad_utility::Timer& requestTimer,
      const ad_utility::httpUtils::HttpRequest auto& request, auto&& send,
      TimeLimit timeLimit);

  static json composeErrorResponseJson(
      const string& query, const std::string& errorMsg,
      ad_utility::Timer& requestTimer,
      const std::optional<ExceptionMetadata>& metadata = std::nullopt);

  json composeStatsJson() const;

  json composeCacheStatsJson() const;

  // Perform the following steps: Acquire a token from the
  // queryProcessingSemaphore_, run `function`, and release the token. These
  // steps are performed on a new thread (not one of the server threads).
  // Returns an awaitable of the return value of `function`
  template <typename Function, typename T = std::invoke_result_t<Function>>
  Awaitable<T> computeInNewThread(Function function) const;

  /// This method extracts a client-defined query id from the passed HTTP
  /// request if it is present. If it is not present or empty, a new
  /// pseudo-random id will be chosen by the server. Note that this id is not
  /// communicated to the client in any way. It ensures that every query has a
  /// unique id and therefore that the code doesn't need to check for an empty
  /// case. In the case of conflict when using a manual id, a
  /// `QueryAlreadyInUseError` exception is thrown.
  ///
  /// \param request The HTTP request to extract the id from.
  ///
  /// \return An OwningQueryId object. It removes itself from the registry
  ///         on destruction.
  ad_utility::websocket::OwningQueryId getQueryId(
      const ad_utility::httpUtils::HttpRequest auto& request);

  /// Schedule a task to trigger the timeout after the `timeLimit`.
  /// The returned callback can be used to prevent this task from executing
  /// either because the `cancellationHandle` has been aborted by some other
  /// means or because the task has been completed successfully.
  static auto cancelAfterDeadline(
      const net::any_io_executor& executor,
      std::weak_ptr<ad_utility::CancellationHandle<>> cancellationHandle,
      TimeLimit timeLimit)
      -> ad_utility::InvocableWithExactReturnType<void> auto;

  /// Acquire the cancellation handle based on `queryId`, pass it to the
  /// operation and configure it to get cancelled automatically by calling
  /// `cancelAfterDeadline`. The return value can be used to cancel the timer.
  auto setupCancellationHandle(const net::any_io_executor& executor,
                               const ad_utility::websocket::QueryId& queryId,
                               const std::shared_ptr<Operation>& rootOperation,
                               TimeLimit timeLimit) const
      -> ad_utility::InvocableWithExactReturnType<void> auto;

  /// Run the SPARQL parser and then the query planner on the `query`. All
  /// computation is performed on the `threadPool_`.
  net::awaitable<PlannedQuery> parseAndPlan(const std::string& query,
                                            QueryExecutionContext& qec) const;

  /// Check if the access token is valid. Return true if the access token
  /// exists and is valid. Return false if there's no access token passed.
  /// Throw an exception if there is a token passed but it doesn't match,
  /// or there is no access token set by the server config. The error message is
  /// formulated towards end users, it can be sent directly as the text of an
  /// HTTP error response.
  bool checkAccessToken(std::optional<std::string_view> accessToken) const;

  /// Checks if a URL parameter exists in the request, if we are allowed to
  /// access it and it matches the expected `value`. If yes, return the value,
  /// otherwise return `std::nullopt`. If `value` is `std::nullopt`, only check
  /// if the key exists. We need this because we have parameters like
  /// "cmd=stats", where a fixed combination of the key and value determines the
  /// kind of action, as well as parameters like "index-decription=...", where
  /// the key determines the kind of action. If the key is not found, always
  /// return `std::nullopt`. If `accessAllowed` is false and a value is present,
  /// throw an exception.
  static std::optional<std::string_view> checkParameter(
      const ad_utility::HashMap<std::string, std::string>& parameters,
      std::string_view key, std::optional<std::string_view> value,
      bool accessAllowed);

  /// Check if user-provided timeout is authorized with a valid access-token or
  /// lower than the server default. Return an empty optional and send a 403
  /// Forbidden HTTP response if the change is not allowed. Return the new
  /// timeout otherwise.
  net::awaitable<std::optional<Server::TimeLimit>>
  verifyUserSubmittedQueryTimeout(
      std::optional<std::string_view> userTimeout, bool accessTokenOk,
      const ad_utility::httpUtils::HttpRequest auto& request, auto& send) const;
};
