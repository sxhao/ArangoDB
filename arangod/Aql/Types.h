////////////////////////////////////////////////////////////////////////////////
/// @brief fundamental types for the optimisation and execution of AQL
///
/// @file arangod/Aql/Types.h
///
/// DISCLAIMER
///
/// Copyright 2010-2014 triagens GmbH, Cologne, Germany
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
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Max Neunhoeffer
/// @author Copyright 2014, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_TYPES_H
#define ARANGODB_AQL_TYPES_H 1

#include <Basics/JsonHelper.h>

#include "VocBase/document-collection.h"
#include "Aql/AstNode.h"
#include "Aql/Variable.h"
#include "V8/v8-conv.h"

namespace triagens {
  namespace aql {

// -----------------------------------------------------------------------------
// --SECTION--                                                            AqlDoc
// -----------------------------------------------------------------------------

    typedef unsigned int RegisterId;

    class AqlItemBlock;

    struct AqlValue {

      enum AqlValueType {
        JSON,
        DOCVEC,
        RANGE
      };

      struct Range {
        int64_t _low;
        int64_t _high;
        Range(int64_t low, int64_t high) : _low(low), _high(high) {}
      };

      union {
        triagens::basics::Json*     _json;
        std::vector<AqlItemBlock*>* _vector;
        Range                       _range;
      };
      
      AqlValueType _type;

      AqlValue (triagens::basics::Json* json)
        : _json(json), _type(JSON) {
      }

      AqlValue (std::vector<AqlItemBlock*>* vector)
        : _vector(vector), _type(DOCVEC) {
      }

      AqlValue (int64_t low, int64_t high) 
        : _range(low, high), _type(RANGE) {
      }

      ~AqlValue ();

      AqlValue* clone () const;

      v8::Handle<v8::Value> toV8 () const {
        switch (_type) {
          case JSON: {
            return TRI_ObjectJson(_json->json());
          }
          case DOCVEC: {
            THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
          }
          case RANGE: {
            v8::Handle<v8::Array> values = v8::Array::New();
            // TODO: fill range
            THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
            return values;
          }
          default: {
            THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
          }
        }
      }

      std::string toString () {
        switch (_type) {
          case JSON: {
            return _json->toString();
          }
          case DOCVEC: {
            std::stringstream s;
            s << "I am a DOCVEC with " << _vector->size() << " blocks.";
            return s.str();
          }
          case RANGE: {
            std::stringstream s;
            s << "I am a range: " << _range._low << " .. " << _range._high;
            return s.str();
          }
          default:
            return std::string("");
        }
      }

    };

// -----------------------------------------------------------------------------
// --SECTION--                                                      AqlItemBlock
// -----------------------------------------------------------------------------

    class AqlItemBlock {

        AqlValue** _data;
        size_t     _nrItems;
        RegisterId _nrRegs;

      public:

        AqlItemBlock (size_t nrItems, RegisterId nrRegs)
          : _nrItems(nrItems), _nrRegs(nrRegs) {
          if (nrItems > 0 && nrRegs > 0) {
            _data = new AqlValue* [nrItems * nrRegs];
            for (size_t i = 0; i < nrItems * nrRegs; i++) {
              _data[i] = nullptr;
            }
          }
          else {
            _data = nullptr;
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

        ~AqlItemBlock () {
          std::unordered_set<AqlValue*> cache;
          if (_data != nullptr) {
            for (size_t i = 0; i < _nrItems * _nrRegs; i++) {
              if (_data[i] != nullptr) {
                auto it = cache.find(_data[i]);
                if (it == cache.end()) {
                  cache.insert(_data[i]);
                  delete _data[i];
                }
              }
            }
            delete[] _data;
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getValue, get the value of a variable
////////////////////////////////////////////////////////////////////////////////

      AqlValue* getValue (size_t index, RegisterId varNr) {
        if (_data == nullptr) {
          return nullptr;
        }
        else {
          return _data[index * _nrRegs + varNr];
        }
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief setValue, set the current value of a variable or attribute
////////////////////////////////////////////////////////////////////////////////

      void setValue (size_t index, RegisterId varNr, AqlValue* zeug) {
        if (_data != nullptr) {
          _data[index * _nrRegs + varNr] = zeug;
        }
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _nrRegs
////////////////////////////////////////////////////////////////////////////////

      RegisterId getNrRegs () {
        return _nrRegs;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _nrItems
////////////////////////////////////////////////////////////////////////////////

      size_t size () {
        return _nrItems;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _data
////////////////////////////////////////////////////////////////////////////////

      AqlValue** getData () {
        return _data;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief slice/clone
////////////////////////////////////////////////////////////////////////////////

      AqlItemBlock* slice (size_t from, size_t to) {
        TRI_ASSERT(from < to && to <= _nrItems);
        std::unordered_map<AqlValue*, AqlValue*> cache;
        auto res = new AqlItemBlock(to-from, _nrRegs);
        for (size_t row = from; row < to; row++) {
          for (RegisterId col = 0; col < _nrRegs; col++) {
            AqlValue* a = _data[row * _nrRegs + col];
            auto it = cache.find(a);
            if (it == cache.end()) {
              AqlValue* b = a->clone();
              res->_data[(row-from) * _nrRegs + col] = b;
              cache.insert(make_pair(a,b));
            }
            else {
              res->_data[(row-from) * _nrRegs + col] = it->second;
            }
          }
        }
        return res;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief splice multiple blocks, note that the new block now owns all
/// AqlValue pointers in the old blocks, therefore, the latter are all
/// set to nullptr, just to be sure.
////////////////////////////////////////////////////////////////////////////////

      static AqlItemBlock* splice(std::vector<AqlItemBlock*>& blocks);

    };

  }  // namespace triagens::aql
}  // namespace triagens

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
