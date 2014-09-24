////////////////////////////////////////////////////////////////////////////////
/// @brief simple http client
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2009-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "SimpleHttpClient.h"

#include "Basics/StringUtils.h"
#include "Basics/logging.h"

#include "GeneralClientConnection.h"
#include "SimpleHttpResult.h"

using namespace triagens::basics;
using namespace triagens::rest;
using namespace std;

namespace triagens {
  namespace httpclient {

// -----------------------------------------------------------------------------
// constructors and destructors
// -----------------------------------------------------------------------------

    SimpleHttpClient::SimpleHttpClient (GeneralClientConnection* connection,
                                        double requestTimeout,
                                        bool warn) :
      _connection(connection),
      _keepConnectionOnDestruction(false),
      _writeBuffer(TRI_UNKNOWN_MEM_ZONE),
      _readBuffer(TRI_UNKNOWN_MEM_ZONE),
      _readBufferOffset(0),
      _requestTimeout(requestTimeout),
      _warn(warn),
      _state(IN_CONNECT),
      _written(0),
      _errorMessage(""),
      _locationRewriter({nullptr, nullptr}),
      _nextChunkedSize(0),
      _result(nullptr),
      _maxPacketSize(128 * 1024 * 1024),
      _keepAlive(true) {

      if (_connection->isConnected()) {
        _state = FINISHED;
      }
    }

    SimpleHttpClient::~SimpleHttpClient () {
      if (! _keepConnectionOnDestruction || ! _connection->isConnected()) {
        _connection->disconnect();
      }
    }

// -----------------------------------------------------------------------------
    // public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief close connection
////////////////////////////////////////////////////////////////////////////////

    bool SimpleHttpClient::close () {
      _connection->disconnect();
      _state = IN_CONNECT;

      reset();

      return true;
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief send out a request, creating a new HttpResult object
////////////////////////////////////////////////////////////////////////////////

    SimpleHttpResult* SimpleHttpClient::request (rest::HttpRequest::HttpRequestType method,
            std::string const& location,
            char const* body,
            size_t bodyLength,
            std::map<std::string, std::string> const& headerFields) {

      TRI_ASSERT(_result == nullptr);

      _result = new SimpleHttpResult;

      _errorMessage = "";

      // set body
      setRequest(method, rewriteLocation(location), body, bodyLength, headerFields);

      TRI_ASSERT(_state == IN_CONNECT || _state == IN_WRITE);

      double endTime = now() + _requestTimeout;
      double remainingTime = _requestTimeout;

      while (isWorking() && remainingTime > 0.0) {
        switch (_state) {
          case (IN_CONNECT): {
            handleConnect();
            break;
          }

          case (IN_WRITE): {
            size_t bytesWritten = 0;

            TRI_set_errno(TRI_ERROR_NO_ERROR);
            if (! _connection->handleWrite(remainingTime, (void*) (_writeBuffer.c_str() + _written), _writeBuffer.length() - _written, &bytesWritten)) {
              if (TRI_errno() != TRI_ERROR_NO_ERROR) {
                setErrorMessage(TRI_last_error(), false);
              }
              this->close();
            }
            else {
              _written += bytesWritten;
              if (_written == _writeBuffer.length())  {
                _state = IN_READ_HEADER;
              }
            }
            break;
          }

          case (IN_READ_HEADER):
          case (IN_READ_BODY):
          case (IN_READ_CHUNKED_HEADER):
          case (IN_READ_CHUNKED_BODY): {
            TRI_set_errno(TRI_ERROR_NO_ERROR);

            if (_connection->handleRead(remainingTime, _readBuffer)) {
              switch (_state) {
                case (IN_READ_HEADER):
                  readHeader();
                  break;
                case (IN_READ_BODY):
                  readBody();
                  break;
                case (IN_READ_CHUNKED_HEADER):
                  readChunkedHeader();
                  break;
                case (IN_READ_CHUNKED_BODY):
                  readChunkedBody();
                  break;
                default:
                  break;
              }
            }
            else {
              if (! _result->hasContentLength() &&
                  ! _connection->isConnected() &&
                  _state == IN_READ_BODY) {
                // no content-length header in response, now set the length
                _result->setContentLength(_readBuffer.length() - _readBufferOffset);
                readBody();
                break;
              }

              if (TRI_errno() != TRI_ERROR_NO_ERROR) {
                setErrorMessage(TRI_last_error(), false);
              }
              this->close();
            }
            break;
          }
          default:
            break;
        }

        remainingTime = endTime - now();
      }

      if (isWorking() && _errorMessage.empty()) {
        setErrorMessage("Request timeout reached");
      }

      // set result type in getResult()
      SimpleHttpResult* result = getResult();

      _result = nullptr;

      return result;
    }

// -----------------------------------------------------------------------------
    // private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise the connection
////////////////////////////////////////////////////////////////////////////////

      void SimpleHttpClient::handleConnect () {
        if (! _connection->connect()) {
          setErrorMessage("Could not connect to '" +  _connection->getEndpoint()->getSpecification() + "'", errno);
          _state = DEAD;
        }
        else {
          // can write now
          _state = IN_WRITE;
          _written = 0;
        }
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief reset state
////////////////////////////////////////////////////////////////////////////////

    void SimpleHttpClient::reset () {
      _readBuffer.clear();
      _readBufferOffset = 0;

      if (_result) {
        _result->clear();
      }
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief sets username and password
////////////////////////////////////////////////////////////////////////////////

    void SimpleHttpClient::setUserNamePassword (const string& prefix,
                                                const string& username,
                                                const string& password) {

      string value = triagens::basics::StringUtils::encodeBase64(username + ":" + password);

      _pathToBasicAuth.push_back(make_pair(prefix, value));
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief return the result
////////////////////////////////////////////////////////////////////////////////

    SimpleHttpResult* SimpleHttpClient::getResult () {
      switch (_state) {
        case IN_WRITE:
          _result->setResultType(SimpleHttpResult::WRITE_ERROR);
          break;

        case IN_READ_HEADER:
        case IN_READ_BODY:
        case IN_READ_CHUNKED_HEADER:
        case IN_READ_CHUNKED_BODY:
          _result->setResultType(SimpleHttpResult::READ_ERROR);
          break;

        case FINISHED:
          _result->setResultType(SimpleHttpResult::COMPLETE);
          break;

        case IN_CONNECT:
        default: {
          _result->setResultType(SimpleHttpResult::COULD_NOT_CONNECT);
          setErrorMessage("Could not connect");
          break;
        }
      }

      return _result;
    }

////////////////////////////////////////////////////////////////////////////////
/// @brief prepare a request
////////////////////////////////////////////////////////////////////////////////

    void SimpleHttpClient::setRequest (rest::HttpRequest::HttpRequestType method,
                                       std::string const& location,
                                       char const* body,
                                       size_t bodyLength,
                                       std::map<std::string, std::string> const& headerFields) {
      _readBufferOffset = 0;
      _readBuffer.reset();

      _method = method;

      // now fill the write buffer
      _writeBuffer.clear();

      // append method
      HttpRequest::appendMethod(method, &_writeBuffer);

      // append location
      string l(location);
      if (location.empty() || location[0] != '/') {
        l = "/" + location;
      }

      _writeBuffer.appendText(l);

      // append protocol
      static char const* ProtocolHeader = " HTTP/1.1\r\n";
      _writeBuffer.appendText(ProtocolHeader, strlen(ProtocolHeader));

      // append hostname
      string&& hostname = _connection->getEndpoint()->getHost();

      static char const* HostHeader = "Host: ";
      _writeBuffer.appendText(HostHeader, strlen(HostHeader));
      _writeBuffer.appendText(hostname);
      _writeBuffer.appendText("\r\n", 2);

      if (_keepAlive) {
        static char const* ConnectionKeepAliveHeader = "Connection: Keep-Alive\r\n";
        _writeBuffer.appendText(ConnectionKeepAliveHeader, strlen(ConnectionKeepAliveHeader));
      }
      else {
        static char const* ConnectionCloseHeader = "Connection: Close\r\n";
        _writeBuffer.appendText(ConnectionCloseHeader, strlen(ConnectionCloseHeader));
      }
      _writeBuffer.appendText("User-Agent: ArangoDB\r\n");
      _writeBuffer.appendText("X-Arango-Version: 2.0\r\n");
      _writeBuffer.appendText("Accept-Encoding: deflate\r\n");

      // do basic authorization
      if (! _pathToBasicAuth.empty()) {
        string foundPrefix;
        string foundValue;
        std::vector<std::pair<std::string, std::string>>::iterator i = _pathToBasicAuth.begin();
        for (; i != _pathToBasicAuth.end(); ++i) {
          string& f = i->first;
          if (l.find(f) == 0) {
            // f is prefix of l
            if (f.length() > foundPrefix.length()) {
              foundPrefix = f;
              foundValue = i->second;
            }
          }
        }
        if (! foundValue.empty() > 0) {
          _writeBuffer.appendText("Authorization: Basic ");
          _writeBuffer.appendText(foundValue);
          _writeBuffer.appendText("\r\n", 2);
        }
      }

      for (auto const& header : headerFields) {
        // TODO: check Header name and value
        _writeBuffer.appendText(header.first);
        _writeBuffer.appendText(": ", strlen(": "));
        _writeBuffer.appendText(header.second);
        _writeBuffer.appendText("\r\n", 2);
      }

      if (method != HttpRequest::HTTP_REQUEST_GET) {
        static char const* ContentLengthHeader = "Content-Length: ";
        _writeBuffer.appendText(ContentLengthHeader, strlen(ContentLengthHeader));
        _writeBuffer.appendInteger(bodyLength);
        _writeBuffer.appendText("\r\n\r\n", 4);
      }
      else {
        _writeBuffer.appendText("\r\n", 2);
      }

      if (body != nullptr) {
        _writeBuffer.appendText(body, bodyLength);
      }

      LOG_TRACE("Request: %s", _writeBuffer.c_str());

      if (_state == DEAD) {
        _connection->resetNumConnectRetries();
      }


      if (_state != FINISHED) {
        // close connection to reset all read and write buffers
        this->close();
      }

      if (_connection->isConnected()) {
        // we are connected, start with writing
        _state = IN_WRITE;
        _written = 0;
      }
      else {
        // connect to server
        _state = IN_CONNECT;
      }

      TRI_ASSERT(_state == IN_CONNECT || _state == IN_WRITE);
    }


// -----------------------------------------------------------------------------
    // private methods
// -----------------------------------------------------------------------------

    bool SimpleHttpClient::readHeader () {
      size_t remain = _readBuffer.length() - _readBufferOffset;
      char const* ptr = _readBuffer.c_str() + _readBufferOffset;
      char const* pos = (char*) memchr(ptr, '\n', remain);

      while (pos) {
        if (pos > ptr && *(pos - 1) == '\r') {
          // adjust eol position
          --pos;
        }

        if (*ptr == '\r' || *ptr == '\0') {
          // end of header found

          size_t len = pos - (_readBuffer.c_str() + _readBufferOffset);
          _readBufferOffset += (len + 1);

          if (*pos == '\r') {
            // adjust offset if line ended with \r\n
            ++_readBufferOffset;
          }
  //        _readBuffer.move_front(len + 1);

          if (_result->isChunked()) {
            _state = IN_READ_CHUNKED_HEADER;
            return readChunkedHeader();
          }
          else if (! _result->hasContentLength()) {
            // no content-length header in response
            _state = IN_READ_BODY;
            return readBody();
          }
          else if (_result->hasContentLength() && _result->getContentLength() > 0) {
            // found content-length header in response
            if (_result->getContentLength() > _maxPacketSize) {
              setErrorMessage("Content-Length > max packet size found", true);

              // reset connection
              this->close();
              _state = DEAD;

              return false;
            }

            _state = IN_READ_BODY;
            return readBody();
          }
          else {
            _result->setResultType(SimpleHttpResult::COMPLETE);
            _state = FINISHED;

            if (! _keepAlive) {
              _connection->disconnect();
            }
          }
          break;
        }
        else {
          size_t len = pos - ptr;
          _result->addHeaderField(ptr, len);

          if (*pos == '\r') {
            // adjust length if line ended with \r\n
            // (header was already added so no harm is done)
            ++len;
          }
        
          ptr += len + 1;
          
          TRI_ASSERT(remain >= (len + 1));
          remain -= (len + 1);

          pos = (char*) memchr(ptr, '\n', remain);
          if (pos == nullptr) {
            // reached the end
            // _readBuffer.move_front((ptr - _readBuffer.c_str()) + 1);
            _readBufferOffset += ((ptr - (_readBuffer.c_str() + _readBufferOffset)) + 1);
          } 
        }
      }

      return true;
    }


    bool SimpleHttpClient::readBody () {
      if (_method == HttpRequest::HTTP_REQUEST_HEAD) {
        // HEAD requests may be responded to without a body...
        _result->setResultType(SimpleHttpResult::COMPLETE);
        _state = FINISHED;
        if (! _keepAlive) {
          _connection->disconnect();
        }
        return true;
      }

      if (_result->hasContentLength() && 
          _readBuffer.length() - _readBufferOffset >= _result->getContentLength()) {

        if (_result->isDeflated()) {
          // body is compressed using deflate. inflate it
          _readBuffer.inflate(_result->getBody());
        }
        else {
          // body is not compressed
          size_t len = _result->getContentLength();
          if (len > _readBuffer.length() - _readBufferOffset) {
            // prevent reading across the string-buffer end
            len = _readBuffer.length() - _readBufferOffset;
          }

          _result->getBody().appendText(_readBuffer.c_str() + _readBufferOffset, len);
        }

        // _readBuffer.move_front(_result->getContentLength());
        _readBufferOffset += _result->getContentLength();
        _result->setResultType(SimpleHttpResult::COMPLETE);
        _state = FINISHED;
        if (! _keepAlive) {
          _connection->disconnect();
        }
      }

      return true;
    }

    bool SimpleHttpClient::readChunkedHeader () {
      size_t remain = _readBuffer.length() - _readBufferOffset;
      char const* ptr = _readBuffer.c_str() + _readBufferOffset;
      char* pos = (char*) memchr(ptr, '\n', remain);

      while (pos) {
        // got a line
        if (pos > ptr && *(pos - 1) == '\r') {
          // adjust eol position
          --pos;
        }

        size_t len = pos - (_readBuffer.c_str() + _readBufferOffset);
        string line(_readBuffer.c_str() + _readBufferOffset, len);
        StringUtils::trimInPlace(line);

      //  _readBuffer.move_front(len + 1);
        _readBufferOffset += (len + 1);
          
        if (*pos == '\r') {
          // adjust offset if line ended with \r\n
          ++_readBufferOffset;
          ++len;
        }

        if (line[0] == '\r' || line.empty()) {
          // ignore empty lines
          remain -= (len + 1);
          ptr += len + 1;

          pos = (char*) memchr(ptr, '\n', remain);
          continue;
        }

        uint32_t contentLength;
        try {
          contentLength = static_cast<uint32_t>(std::stol(line, nullptr, 16)); // C++11
        }
        catch (...) {
          setErrorMessage("found invalid content-length", true);
          // reset connection
          this->close();
          _state = DEAD;

          return false;
        }

        if (contentLength == 0) {
          // OK: last content length found
          _result->setResultType(SimpleHttpResult::COMPLETE);

          _state = FINISHED;
          if (! _keepAlive) {
            _connection->disconnect();
          }

          return true;
        }

        if (contentLength > _maxPacketSize) {
          // failed: too many bytes

          setErrorMessage("Content-Length > max packet size found!", true);
          // reset connection
          this->close();
          _state = DEAD;

          return false;
        }

        _state = IN_READ_CHUNKED_BODY;
        _nextChunkedSize = contentLength;

        return readChunkedBody();
      }

      return true;
    }

    bool SimpleHttpClient::readChunkedBody () {
      if (_method == HttpRequest::HTTP_REQUEST_HEAD) {
        // HEAD requests may be responded to without a body...
        _result->setResultType(SimpleHttpResult::COMPLETE);
        _state = FINISHED;
        if (! _keepAlive) {
          _connection->disconnect();
        }
        return true;
      }

      if (_readBuffer.length() - _readBufferOffset >= _nextChunkedSize) {
        _result->getBody().appendText(_readBuffer.c_str() + _readBufferOffset,
                                      (size_t) _nextChunkedSize);
        // _readBuffer.move_front((size_t) _nextChunkedSize);
        _readBufferOffset += (size_t) _nextChunkedSize;
        _state = IN_READ_CHUNKED_HEADER;
        return readChunkedHeader();
      }

      return true;
    }

  }

}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
