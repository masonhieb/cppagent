//
// Copyright Copyright 2009-2021, AMT – The Association For Manufacturing Technology (“AMT”)
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

#include <boost/asio.hpp>

#include "connector.hpp"

#include <dlib/logger.h>

#include <boost/bind.hpp>
#include <boost/bind/bind.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <utility>
#include <functional>

#include <boost/log/attributes.hpp>
#include <boost/log/trivial.hpp>

using namespace std;
using namespace std::chrono;
using namespace dlib;
using namespace std::chrono_literals;
using namespace boost::placeholders;

namespace asio = boost::asio;
namespace ip = asio::ip;
namespace sys = boost::system;

namespace mtconnect
{
  namespace adapter
  {
    // Connector public methods
    Connector::Connector(asio::io_context &context,
                         string server, unsigned int port, seconds legacyTimeout,
                         seconds reconnectInterval)
      : m_server(std::move(server)),
        m_strand(context),
        m_socket(context),
        m_port(port),
        m_localPort(0),
        m_incoming(1024 * 1024),
        m_timer(context),
        m_heartbeatTimer(context),
        m_receiveTimeout(context),
        m_connected(false),
        m_realTime(false),
        m_legacyTimeout(duration_cast<milliseconds>(legacyTimeout)),
        m_reconnectInterval(duration_cast<milliseconds>(reconnectInterval)),
        m_receiveTimeLimit(m_legacyTimeout)
    {
    }

    Connector::~Connector()
    {
      m_socket.cancel();
      if (m_socket.is_open())
        m_socket.close();
    }
    
    bool Connector::start()
    {
      return resolve() && connect();
    }
    
    bool Connector::resolve()
    {
      boost::system::error_code ec;
      
      ip::tcp::resolver resolve(m_strand.context());
      m_results = resolve.resolve(m_server, to_string(m_port), ec);
      if (ec)
      {
        BOOST_LOG_TRIVIAL(error) << "Cannot resolve address: " << m_server << ":" << m_port;
        BOOST_LOG_TRIVIAL(error) << ec.category().message(ec.value()) << ": "
        << ec.message();
        return false;
      }
      
      return true;
    }
    
    bool Connector::connect()
    {
      BOOST_LOG_NAMED_SCOPE("input.connector");

      m_connected = false;
      connecting();
      using boost::placeholders::_1;
      using boost::placeholders::_2;

      // Connect to server:port, failure will throw dlib::socket_error exception
      // Using a smart pointer to ensure connection is deleted if exception thrown
      BOOST_LOG_TRIVIAL(debug) << "Connecting to data source: " << m_server << " on port: " << m_port;

      asio::async_connect(m_socket, m_results.begin(),
                          m_results.end(),
                          asio::bind_executor(m_strand, boost::bind(&Connector::connected, this, _1, _2)));
      
      return true;
    }
    
    inline void Connector::asyncTryConnect()
    {
      m_timer.expires_from_now(m_reconnectInterval);
      m_timer.async_wait(asio::bind_executor(m_strand, [this](boost::system::error_code ec) {
        if (ec != boost::asio::error::operation_aborted)
        {
          BOOST_LOG_TRIVIAL(info) << "reconnect: retrying connection";
          connect();
        }
      }));
    }
    
    void Connector::reconnect()
    {
      BOOST_LOG_TRIVIAL(info) << "reconnect: retry connection in "
        << m_reconnectInterval.count() << "ms";
      close();
      asyncTryConnect();
    }
    
    void Connector::connected(const boost::system::error_code& ec,
                              ip::tcp::resolver::iterator it)
    {
      if (ec)
      {
        BOOST_LOG_TRIVIAL(error) << ec.category().message(ec.value()) << ": "
        << ec.message();
        asyncTryConnect();
      }
      else
      {
        BOOST_LOG_TRIVIAL(info) << "Connected with: " << m_socket.remote_endpoint();
        m_timer.cancel();
        
        m_socket.set_option(asio::ip::tcp::no_delay(true));
        m_socket.set_option(asio::socket_base::linger(false, 0));
        m_socket.set_option(asio::socket_base::keep_alive(true));
        m_localPort = m_socket.local_endpoint().port();
        
        connected();
        m_connected = true;
        sendCommand("PING");
        
        reader(sys::error_code(), 0);
      }
    }
    
#include <boost/asio/yield.hpp>
    
    void Connector::reader(sys::error_code ec, size_t len)
    {
      if (!m_connected) return;
      
      if (ec)
      {
        BOOST_LOG_TRIVIAL(error) << "reader: " << ec.category().message(ec.value()) << ": "
        << ec.message();
        reconnect();
      }
      else
      {
        reenter(m_coroutine)
        {
          while (m_connected && m_socket.is_open())
          {
            m_timer.expires_from_now(m_receiveTimeLimit);
            m_timer.async_wait(asio::bind_executor(m_strand, [this](boost::system::error_code ec) {
              if (ec != boost::asio::error::operation_aborted)
              {
                BOOST_LOG_TRIVIAL(warning) << "reader: operation timed out after " << m_receiveTimeLimit.count() << "ms";
                reconnect();
              }
            }));
            yield asio::async_read_until(m_socket, m_incoming, '\n',
                               asio::bind_executor(m_strand,
                                 boost::bind(&Connector::reader, this, _1, _2)));
            m_timer.cancel();
            parseSocketBuffer();
          }
          reconnect();
        }
      }
    }

#include <boost/asio/unyield.hpp>

    void Connector::writer(sys::error_code ec, size_t lenght)
    {
      if (ec)
      {
        BOOST_LOG_TRIVIAL(error) << "writer: " << ec.category().message(ec.value()) << ": "
        << ec.message();
        close();
      }
    }

    void Connector::parseBuffer(const char *buffer)
    {
      std::ostream os(&m_incoming);
      os << buffer;
      while (m_incoming.size() > 0)
        parseSocketBuffer();
    }
    
    inline void Connector::setReceiveTimeout()
    {
      m_receiveTimeout.expires_from_now(m_receiveTimeLimit);
      m_receiveTimeout.async_wait([this](sys::error_code ec) {
        if (!ec)
        {
          BOOST_LOG_TRIVIAL(error) << "(Port:" << m_localPort << ")"
                   << " connect: Did not receive data for over: "
                   << m_receiveTimeLimit.count() << " ms";
          close();
        }
        else if (ec != boost::asio::error::operation_aborted)
        {
          BOOST_LOG_TRIVIAL(error) << "Receive timeout: " << ec.category().message(ec.value()) << ": "
                   << ec.message();
        }
      });
    }

    void Connector::parseSocketBuffer()
    {
      // Cancel receive time limit
      setReceiveTimeout();
      
      // Treat any data as a heartbeat.
      istream is(&m_incoming);
      string line;
      getline(is, line);

      if (line.empty())
        return;

      auto end = line.find_last_not_of(" \t\n\r");
      if (end != string::npos)
        line.erase(end + 1);

      // Check for heartbeats
      if (line[0] == '*')
      {
        if (!line.compare(0, 6, "* PONG"))
        {
          BOOST_LOG_TRIVIAL(debug) << "(Port:" << m_localPort << ")"
                     << " Received a PONG for " << m_server << " on port " << m_port;
          if (!m_heartbeats)
            startHeartbeats(line);
        }
        else
        {
          protocolCommand(line);
        }
      }
      else
      {
        processData(line);
      }
    }

    void Connector::sendCommand(const string &command)
    {
      if (m_connected)
      {
        BOOST_LOG_TRIVIAL(debug) << "(Port:" << m_localPort << ") "
                  << "Sending " << command;
        ostream os(&m_outgoing);
        os << "* " << command << "\n";
        asio::async_write(m_socket, m_outgoing,
                          asio::bind_executor(m_strand, boost::bind(&Connector::writer, this, _1, _2)));
      }
    }

    void Connector::heartbeat(boost::system::error_code ec)
    {
      if (!ec)
      {
        BOOST_LOG_TRIVIAL(debug) << "Sending heartbeat";
        sendCommand("* PING");
        m_heartbeatTimer.expires_from_now(m_heartbeatFrequency);
        m_heartbeatTimer.async_wait(asio::bind_executor(m_strand, boost::bind(&Connector::heartbeat, this, _1)));
      }
      else if (ec != boost::asio::error::operation_aborted)
      {
        BOOST_LOG_TRIVIAL(error) << "heartbeat: " << ec.category().message(ec.value()) << ": "
                 << ec.message();
      }
    }

    void Connector::startHeartbeats(const string &arg)
    {
      size_t pos;
      if (arg.length() > 7 && arg[6] == ' ' &&
          (pos = arg.find_first_of("0123456789", 7)) != string::npos)
      {
        auto freq = milliseconds {atoi(arg.substr(pos).c_str())};
        constexpr minutes maxTimeOut = minutes {30};  // Make the maximum timeout 30 minutes.

        if (freq > 0ms && freq < maxTimeOut)
        {
          BOOST_LOG_TRIVIAL(debug) << "(Port:" << m_localPort << ")"
                   << "Received PONG, starting heartbeats every " << freq.count() << "ms";
          m_heartbeats = true;
          m_heartbeatFrequency = freq;
          m_receiveTimeLimit = 2 * m_heartbeatFrequency;
          setReceiveTimeout();

          m_heartbeatTimer.expires_from_now(m_heartbeatFrequency);
          m_heartbeatTimer.async_wait(asio::bind_executor(m_strand, boost::bind(&Connector::heartbeat, this, _1)));
          
        }
        else
        {
          BOOST_LOG_TRIVIAL(error) << "(Port:" << m_localPort << ")"
                   << "startHeartbeats: Bad heartbeat frequency " << arg << ", ignoring";
        }
      }
      else
      {
        BOOST_LOG_TRIVIAL(error) << "(Port:" << m_localPort << ")"
                 << "startHeartbeats: Bad heartbeat command " << arg << ", ignoring";
      }
    }

    void Connector::close()
    {
      m_heartbeatTimer.cancel();
      m_receiveTimeout.cancel();
      m_timer.cancel();
      
      if (m_connected)
      {
        if (m_socket.is_open())
          m_socket.close();
        m_connected = false;
        disconnected();
      }
    }
  }
}
