var Tokens = [];

Pebble.addEventListener("ready",
    function(e) {
        QueueAppMessage({ "utcoffset_set": (new Date()).getTimezoneOffset() * -60});
        QueueAppMessage({ "AMReadCredentialList": 1});
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

var TokenByID = function(id) {
    for (var idx in Tokens) {
        if (Tokens[idx].ID == id) return Tokens[idx];
    }
};

var AMQueue = [];
var AMPending = false;
var QueueAppMessage = function(message) {
    AMQueue.push(message);
    if (!AMPending) AMTransmitQueue();
};

var AMQueueFail = function(txn) {
    console.log("AM transmit failed: " + txn.error.message + ", continuing");
    AMTransmitQueue();
};

var AMTransmitQueue = function() {
    if (!AMQueue.length) {
        AMPending = false;
        return;
    }
    AMPending = true;
    var message = AMQueue.shift();
    Pebble.sendAppMessage(message, AMTransmitQueue, AMQueueFail);
};

Pebble.addEventListener("appmessage",
  function(e) {
    if (e.payload.AMReadCredentialList_Result) {
        var token = {};
        token.ID = e.payload.AMReadCredentialList_Result[0];
        token.Name = unCString(e.payload.AMReadCredentialList_Result, 2);
        Tokens.push(token);
    }
  }
);

Pebble.addEventListener("showConfiguration", function() {
    Pebble.openURL("http://collins-macbook-pro-2.local:8080/config/config.html#" + encodeURIComponent(JSON.stringify(Tokens)));
});

var ReconcileConfiguration = function(newTokens) {
    var existing_ids = [];
    var new_ids = [];
    var idx, token; // So JSLint stops complaining.
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

    console.log("Creating " + JSON.stringify(to_create.map(function(e){return {"Name": e.Name, "ID": e.ID};}))); // Strip the secret so it doesn't appear in logs.
    console.log("Updating " + JSON.stringify(to_update));
    console.log("Deleting " + JSON.stringify(to_delete_ids));

    // Apply updates
    for(idx in to_delete_ids) {
        QueueAppMessage({"AMDeleteCredential": [to_delete_ids[idx]]});
    }
    for (idx in to_create) {
        token = to_create[idx];
        QueueAppMessage({"AMCreateCredential": token.Secret, "AMCreateCredential_ID": token.ID, "AMCreateCredential_Name": token.Name});
        token.Secret = "";
    }
    for (idx in to_update) {
        token = to_update[idx];
        QueueAppMessage({"AMUpdateCredential": [token.ID, 0, token.Name, 0]});
    }
    QueueAppMessage({"AMSetCredentialListOrder": new_ids});
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