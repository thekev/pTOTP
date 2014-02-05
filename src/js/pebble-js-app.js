var Tokens = [];

Pebble.addEventListener("ready",
    function(e) {
        console.log("Hello world! - Sent from your javascript application.");
        setTimeout(function(){Pebble.sendAppMessage( { "utcoffset_set": (new Date()).getTimezoneOffset() * -60});}, 3000);
        //setTimeout(function(){Pebble.sendAppMessage( { "AMClearCredentials": 0, "AMCreateCredential": "test 123", "AMCreateCredential_ID": 1, "AMCreateCredential_Name": "hey-o"});}, 3000);
        setTimeout(function(){Pebble.sendAppMessage( { "AMReadCredentialList": 1});}, 5000);
    }
);

var unCString = function(array, offset) {
    var string = "";
    for (var i = offset; i < array.length; i++){
        if (array[i] === 0) break;
        string += String.fromCharCode(array[i]);
    }
    return string;
};

Pebble.addEventListener("appmessage",
  function(e) {
    console.log("Received message: " + e.payload);
    if (e.payload.AMReadCredentialList_Result) {
        var token = {};
        token.ID = e.payload.AMReadCredentialList_Result[0];
        token.Name = unCString(e.payload.AMReadCredentialList_Result, 2);
        console.log(token.Name);
        Tokens.push(token);
        console.log(JSON.stringify(token));
    }
  }
);

Pebble.addEventListener("showConfiguration", function() {
    Pebble.openURL("http://10.0.126.222:8080/config/config.html#" + encodeURIComponent(JSON.stringify(Tokens)));
});

var TokenByID = function(id) {
    for (var idx in Tokens) {
        if (Tokens[idx].ID == id) return Tokens[idx];
    }
};

var ReconcileConfiguration = function(newTokens) {
    var existing_ids = [];
    var new_ids = [];
    var idx; // So JSLint stops complaining.
    for (idx in Tokens) { existing_ids.push(Tokens[idx].ID); } // I can pretend it's list comprehension, right?
    for (idx in newTokens) { new_ids.push(newTokens[idx].ID); }
    // Which have been deleted (by ID only)?
    var to_delete_ids = [];
    for(idx in existing_ids) {
        if (new_ids.indexOf(existing_ids[idx]) < 0) to_delete_ids.push(existing_ids[idx]);
    }

    // Which have been added?
    // Which have been modified?
    var to_create = [];
    var to_update = [];
    for(idx in newTokens) {
        if (existing_ids.indexOf(newTokens[idx].ID) < 0) {
            to_create.push(newTokens[idx]);
        } else {
            if (TokenByID(newTokens[idx].ID).Name != newTokens[idx].Name) {
                to_update.push(newTokens[idx]);
            }
        }
    }
    // Detect reordering: TODO!

    console.log("Creating " + JSON.stringify(to_create));
    console.log("Updating " + JSON.stringify(to_update));
    console.log("Deleting " + JSON.stringify(to_delete_ids));
    // Apply updates
    for(idx in to_delete_ids) {
        Pebble.sendAppMessage( { "AMDeleteCredential": [to_delete_ids[idx]]});
    }
    for (idx in to_create) {
        var token = to_create[idx];
        Pebble.sendAppMessage({"AMCreateCredential": token.Secret, "AMCreateCredential_ID": token.ID, "AMCreateCredential_Name": token.Name});
    }
    for (idx in to_update) {
        var token = to_update[idx];
        Pebble.sendAppMessage({"AMUpdateCredential": [token.ID, 0, token.Name, 0]});
    }
    Tokens = newTokens;
};

Pebble.addEventListener("webviewclosed", function(e) {
    if (!e.response) return;
    var newTokens = JSON.parse(decodeURIComponent(e.response));
    for (var idx in newTokens) {
        newTokens[idx].ID = +newTokens[idx].ID; // Blegh
    }
    ReconcileConfiguration(newTokens);
});