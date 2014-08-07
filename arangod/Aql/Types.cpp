////////////////////////////////////////////////////////////////////////////////
/// @brief Fundamental objects for the execution of AQL queries
///
/// @file arangod/Aql/Types.cpp
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

#include "Aql/Types.h"

using namespace triagens::aql;
using Json = triagens::basics::Json;

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy, explicit destruction, only when needed
////////////////////////////////////////////////////////////////////////////////

void AqlValue::destroy () {
  switch (_type) {
    case JSON: {
      if (_json != nullptr) {
        delete _json;
      }
      break;
    }
    case DOCVEC: {
      if (_vector != nullptr) {
        for (auto it = _vector->begin(); it != _vector->end(); ++it) {
          delete *it;
        }
        delete _vector;
      }
      delete _vector;
      break;
    }
    case RANGE: {
      delete _range;
      break;
    }
    case SHAPED: {
      // do nothing here, since data pointers need not be freed
      break;
    }
    default: {
      TRI_ASSERT(false);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief clone for recursive copying
////////////////////////////////////////////////////////////////////////////////

AqlValue AqlValue::clone () const {
  switch (_type) {
    case JSON: {
      return AqlValue(new Json(_json->copy()));
    }

    case SHAPED: {
      return AqlValue(_marker);
    }

    case DOCVEC: {
      auto c = new std::vector<AqlItemBlock*>;
      c->reserve(_vector->size());
      for (auto it = _vector->begin(); it != _vector->end(); ++it) {
        c->push_back((*it)->slice(0, (*it)->size()));
      }
      return AqlValue(c);
    }

    case RANGE: {
      return AqlValue(_range->_low, _range->_high);
    }

    default: {
      TRI_ASSERT(false);
    }
  }

}

////////////////////////////////////////////////////////////////////////////////
/// @brief construct a V8 value as input for the expression execution in V8
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> AqlValue::toV8 (AQL_TRANSACTION_V8* trx, 
                                      TRI_document_collection_t const* document) const {
  switch (_type) {
    case JSON: {
      return TRI_ObjectJson(_json->json());
    }

    case SHAPED: {
      return TRI_WrapShapedJson<AQL_TRANSACTION_V8>(*trx, document->_info._cid, _marker);
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

    case EMPTY: {
      return v8::Object::New(); // TODO?
    }
  }
      
  // should never get here
  THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief toString method
////////////////////////////////////////////////////////////////////////////////

std::string AqlValue::toString (TRI_document_collection_t const* document) const {
  switch (_type) {
    case JSON: {
      return _json->toString();
    }

    case SHAPED: {
      // we're lazy and just stringify the json representation
      // this does not matter as this code is not performance-sensitive
      return toJson(document).toString();
    }

    case DOCVEC: {
      std::stringstream s;
      s << "I am a DOCVEC with " << _vector->size() << " blocks.";
      return s.str();
    }

    case RANGE: {
      std::stringstream s;
      s << "I am a range: " << _range->_low << " .. " << _range->_high;
      return s.str();
    }

    case EMPTY: {
      return std::string("empty");
    }
  }
      
  // should never get here
  THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief toJson method
////////////////////////////////////////////////////////////////////////////////
      
Json AqlValue::toJson (TRI_document_collection_t const* document) const {
  switch (_type) {
    case JSON: {
      return *_json;
    }

    case SHAPED: {
      TRI_shaper_t* shaper = document->getShaper();
      TRI_shaped_json_t shaped;
      TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, _marker);
      triagens::basics::Json json(shaper->_memoryZone, TRI_JsonShapedJson(shaper, &shaped));

      char const* key = TRI_EXTRACT_MARKER_KEY(_marker);
      // TODO: use CollectionNameResolver
      std::string id(document->_info._name);
      id.push_back('/');
      id += std::string(key);
      json("_id", triagens::basics::Json(id));
      json("_rev", triagens::basics::Json(std::to_string(TRI_EXTRACT_MARKER_RID(_marker) )));
      json("_key", triagens::basics::Json(key));

      // TODO: return _from and _to, and fix order of attributes!
      return json;
    }
          
    case DOCVEC: {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
    }
          
    case RANGE: {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_NOT_IMPLEMENTED);
    }

    case EMPTY: {
      return triagens::basics::Json();
    }
  }

  THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief splice multiple blocks, note that the new block now owns all
/// AqlValue pointers in the old blocks, therefore, the latter are all
/// set to nullptr, just to be sure.
////////////////////////////////////////////////////////////////////////////////

AqlItemBlock* AqlItemBlock::splice (std::vector<AqlItemBlock*>& blocks) {
  TRI_ASSERT(blocks.size() != 0);

  auto it = blocks.begin();
  TRI_ASSERT(it != blocks.end());
  size_t totalSize = (*it)->size();
  RegisterId nrRegs = (*it)->getNrRegs();

  while (true) {
    if (++it == blocks.end()) {
      break;
    }
    totalSize += (*it)->size();
    TRI_ASSERT((*it)->getNrRegs() == nrRegs);
  }

  auto res = new AqlItemBlock(totalSize, nrRegs);
  size_t pos = 0;
  for (it = blocks.begin(); it != blocks.end(); ++it) {
    if (it == blocks.begin()) {
      for (RegisterId col = 0; col < nrRegs; ++col) {
        res->getDocumentCollections().at(col)
          = (*it)->getDocumentCollections().at(col);
      }
    }
    else {
      for (RegisterId col = 0; col < nrRegs; ++col) {
        TRI_ASSERT(res->getDocumentCollections().at(col) ==
                   (*it)->getDocumentCollections().at(col));
      }
    }
    for (size_t row = 0; row < (*it)->size(); ++row) {
      for (RegisterId col = 0; col < nrRegs; ++col) {
        res->setValue(pos+row, col, (*it)->getValue(row, col));
        (*it)->setValue(row, col, AqlValue());
      }
    }
    pos += (*it)->size();
  }
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief 3-way comparison for AqlValue objects
////////////////////////////////////////////////////////////////////////////////

int triagens::aql::CompareAqlValues ( AqlValue const& left,  
                                      TRI_document_collection_t const* leftcoll,
                                      AqlValue const& right, 
                                      TRI_document_collection_t const* rightcoll ) {

        if (left._type != right._type) {
          if (left._type == AqlValue::EMPTY) {
            return -1;
          }
          if (right._type == AqlValue::EMPTY) {
            return 1;
          }
          if (left._type == AqlValue::JSON && right._type == AqlValue::SHAPED) {
            triagens::basics::Json rjson = right.toJson(rightcoll);
            return TRI_CompareValuesJson(left._json->json(), rjson.json());
          }
          if (left._type == AqlValue::SHAPED && right._type == AqlValue::JSON) {
            triagens::basics::Json ljson = left.toJson(leftcoll);
            return TRI_CompareValuesJson(ljson.json(), right._json->json());
          }
          // No other comparisons are defined
          TRI_ASSERT(false);
        }
        switch (left._type) {
          case AqlValue::EMPTY: {
            return 0;
          }
          case AqlValue::JSON: {
            return TRI_CompareValuesJson(left._json->json(), right._json->json());
          }
          case AqlValue::SHAPED: {
            TRI_shaped_json_t l;
            TRI_shaped_json_t r;
            TRI_EXTRACT_SHAPED_JSON_MARKER(l, left._marker);
            TRI_EXTRACT_SHAPED_JSON_MARKER(r, right._marker);
                      
            return TRI_CompareShapeTypes(nullptr, nullptr, &l, leftcoll->getShaper(), 
                                         nullptr, nullptr, &r, rightcoll->getShaper());
          }
          case AqlValue::DOCVEC: { 
            // use lexicographic ordering of AqlValues regardless of block,
            // DOCVECs have a single register coming from ReturnNode.
            size_t lblock = 0;
            size_t litem = 0;
            size_t rblock = 0;
            size_t ritem = 0;
            
            while( lblock < left._vector->size() && rblock < right._vector->size() ){
              AqlValue lval = left._vector->at(lblock)->getValue(litem, 0);
              AqlValue rval = right._vector->at(rblock)->getValue(ritem, 0);
              int cmp = CompareAqlValues(lval, 
                                         left._vector->at(lblock)->getDocumentCollection(0),
                                         rval, 
                                         right._vector->at(rblock)->getDocumentCollection(0));
              if(cmp != 0){
                return cmp;
              }
              if(++litem == left._vector->size()){
                litem = 0;
                lblock++;
              }
              if(++ritem == right._vector->size()){
                ritem = 0;
                rblock++;
              }
            }

            if(lblock == left._vector->size() && rblock == right._vector->size()){
              return 0;
            }

            return (lblock < left._vector->size()?-1:1);
          }
          case AqlValue::RANGE: {
            if(left._range->_low < right._range->_low){
              return -1;
            } 
            if (left._range->_low > right._range->_low){
              return 1;
            } 
            if (left._range->_high < left._range->_high) {
              return -1;
            } 
            if (left._range->_high > left._range->_high) {
              return 1;
            }
            return 0;
          }
          default: {
            TRI_ASSERT(false);
            return 0;
          }
        }


}

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:


