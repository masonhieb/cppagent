//
// Copyright Copyright 2009-2019, AMT – The Association For Manufacturing Technology (“AMT”)
// All rights reserved.
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
//

#pragma once

#include "test_globals.hpp"
#include "http_server/server.hpp"
#include "http_server/response.hpp"
#include "http_server/routing.hpp"

#include <dlib/server.h>

#include <chrono>
#include <iosfwd>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace mtconnect
{
  class Agent;
  
  namespace http_server
  {
    class TestResponse : public Response
    {
    public:
      using Response::Response;
      
      void writeResponse(const char *body,
                         const size_t size,
                         const ResponseCode code,
                         const std::string &mimeType = "text/plain",
                         const std::chrono::seconds expires = std::chrono::seconds(0)) override
      {
        m_body = std::string(body, size);
        m_code = code;
        m_mimeType = mimeType;
        m_expires = expires;
        Response::writeResponse(body, size, code, mimeType, expires);
      }
      
      void writeMultipartChunk(const std::string &body, const std::string &mimeType) override
      {
        m_chunkBody = body;
        m_chunkMimeType = mimeType;
        Response::writeMultipartChunk(body, mimeType);
      }

      
      std::string getHeaderDate() override
      {
        return "TIME+DATE";
      }
      
      std::string m_body;
      std::string m_mimeType;
      ResponseCode m_code;
      std::chrono::seconds m_expires;
      
      std::string m_chunkBody;
      std::string m_chunkMimeType;
    };

  }
}

namespace http = mtconnect::http_server;

class AgentTestHelper
{
 public:
  AgentTestHelper()
  : m_out(), m_incomingIp("127.0.0.1"), m_response(m_out)
  {
  }
    
  // Helper method to test expected string, given optional query, & run tests
  void responseHelper(const char *file, int line,
                      const mtconnect::http_server::Routing::QueryMap &aQueries,
                      xmlDocPtr *doc, const char *path);
  void responseStreamHelper(const char *file, int line,
                      const mtconnect::http_server::Routing::QueryMap &aQueries,
                      xmlDocPtr *doc, const char *path);
  void responseHelper(const char *file, int line,
                      const mtconnect::http_server::Routing::QueryMap& aQueries, nlohmann::json &doc, const char *path);
  void putResponseHelper(const char *file, int line, const std::string &body,
                         const mtconnect::http_server::Routing::QueryMap &aQueries,
                         xmlDocPtr *doc, const char *path);
  void deleteResponseHelper(const char *file, int line, 
                            const mtconnect::http_server::Routing::QueryMap &aQueries, xmlDocPtr *doc, const char *path);

  void makeRequest(const char *file, int line, const char *verb,
                   const std::string &body,
                   const mtconnect::http_server::Routing::QueryMap &aQueries,
                   const char *path);
  
  auto getAgent() { return m_agent.get(); }
  
  auto createAgent(const std::string &file, int bufferSize = 8, int maxAssets = 4,
                   const std::string &version = "1.7", int checkpoint = 25,
                   bool put = false)
  {
    auto server = std::make_unique<http::Server>();
    server->enablePut(put);
    auto cache = std::make_unique<http::FileCache>();
    m_agent = std::make_unique<mtconnect::Agent>(server, cache,
                                                 PROJECT_ROOT_DIR + file,
                                                 bufferSize, maxAssets, version,
                                                 checkpoint, true);
    return m_agent.get();
  }
  
  void printResponse()
  {
    std::cout << "Status " << m_response.m_code << " "
              << http::Response::getStatus(m_response.m_code) << std::endl
              << m_response.m_body << std::endl << "------------------------"
              << std::endl;
  }

  void printResponseStream()
  {
    std::cout << "Status " << m_response.m_code << " "
              << http::Response::getStatus(m_response.m_code) << std::endl
              << m_out.str() << std::endl << "------------------------"
              << std::endl;
  }

  
  bool m_dispatched { false };
  std::unique_ptr<mtconnect::Agent> m_agent;
  std::stringstream m_out;
  mtconnect::http_server::TestResponse m_response;
  mtconnect::http_server::Routing::Request m_request;
  
  std::string m_incomingIp;
};

#define PARSE_XML_RESPONSE(path)                                                           \
  xmlDocPtr doc = nullptr;                                                                   \
  m_agentTestHelper->responseHelper(__FILE__, __LINE__, {}, &doc, path); \
  ASSERT_TRUE(doc)

#define PARSE_TEXT_RESPONSE(path)                                                           \
  xmlDocPtr doc = nullptr;                                                                   \
  m_agentTestHelper->responseHelper(__FILE__, __LINE__, {}, &doc, path);


#define PARSE_XML_RESPONSE_QUERY(path, queries)                               \
  xmlDocPtr doc = nullptr;                                              \
  m_agentTestHelper->responseHelper(__FILE__, __LINE__, queries, &doc, path); \
  ASSERT_TRUE(doc)

#define PARSE_XML_STREAM_QUERY(path, queries)                               \
  xmlDocPtr doc = nullptr;                                              \
  m_agentTestHelper->responseStreamHelper(__FILE__, __LINE__, queries, &doc, path); \
  ASSERT_TRUE(doc)


#define PARSE_XML_RESPONSE_PUT(path, body, queries)                                    \
  xmlDocPtr doc = nullptr;                                                       \
  m_agentTestHelper->putResponseHelper(__FILE__, __LINE__, body, queries, &doc, path); \
  ASSERT_TRUE(doc)

#define PARSE_XML_RESPONSE_DELETE(path)                                    \
  xmlDocPtr doc = nullptr;                                                       \
  m_agentTestHelper->deleteResponseHelper(__FILE__, __LINE__, {}, &doc, path); \
  ASSERT_TRUE(doc)

#define PARSE_JSON_RESPONSE(path) \
  nlohmann::json doc; \
  m_agentTestHelper->responseHelper(__FILE__, __LINE__, {}, doc, path)

