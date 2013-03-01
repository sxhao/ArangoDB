////////////////////////////////////////////////////////////////////////////////
/// @brief nonces
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// @author Frank Celler
/// @author Achim Brandt
/// @author Copyright 2010-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_BASICS_NONCE_H
#define TRIAGENS_BASICS_NONCE_H 1

#include "Basics/Common.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                            nonces
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Nonce
/// @{
////////////////////////////////////////////////////////////////////////////////

namespace triagens {
  namespace basics {
    namespace Nonce {

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief statistics collection
      ////////////////////////////////////////////////////////////////////////////////

      struct Statistics {
        uint32_t age;
        uint32_t checks;
        uint32_t isUnused;
        uint32_t isUsed;
        uint32_t marked;
        uint32_t falselyUsed;
        double fillingDegree;
      };

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief creates a nonce hash of given size
      ////////////////////////////////////////////////////////////////////////////////

      void create (size_t);

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief destroys the nonce storage
      ////////////////////////////////////////////////////////////////////////////////

      void destroy ();

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief checks if a nonce is valid and invalidates it
      ////////////////////////////////////////////////////////////////////////////////

      bool checkAndMark (uint32_t timestamp, uint64_t random);

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief returns the statistics
      ////////////////////////////////////////////////////////////////////////////////

      vector<Statistics> statistics ();
    }
  }
}
////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End: