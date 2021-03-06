
var db = require("org/arangodb").db;
var internal = require("internal");
var jsunity = require("jsunity");


function runSetup () {
  internal.debugClearFailAt();
  
  var c = [ ], i, j;
  for (i = 0; i < 10; ++i) {
    db._drop("UnitTestsRecovery" + i);
    c[i] = db._create("UnitTestsRecovery" + i);

    for (j = 0; j < 49; ++j) {
      c[i].save({ _key: "test" + j, a: j, b: "test" + j });
    }

    c[i].save({ _key: "test49", a: 49, b: "test49" }, true); // sync 
  }

  internal.debugSetFailAt("WalSlotCrc");

  // now corrupt all the collections
  for (i = 0; i < 10; ++i) {
    c[i].save({ _key: "test50", a: 50, b: "test50" });
  }

  internal.debugSegfault("crashing server");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function recoverySuite () {
  jsunity.jsUnity.attachAssertions();

  return {
    setUp: function () {
    },
    tearDown: function () {
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test whether we can restore the 10 collections
////////////////////////////////////////////////////////////////////////////////
    
    testCorruptwalMarkerMultiple : function () {
      var i, j, c;
      for (i = 0; i < 10; ++i) {
        c = db._collection("UnitTestsRecovery" + i);

        assertEqual(50, c.count());
        for (j = 0; j < 50; ++j) {
          assertEqual(j, c.document("test" + j).a); 
          assertEqual("test" + j, c.document("test" + j).b); 
        }
      }
    }
        
  };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

function main (argv) {
  if (argv[1] === "setup") {
    runSetup();
    return 0;
  }
  else {
    jsunity.run(recoverySuite);
    return jsunity.done().status ? 0 : 1;
  }
}

