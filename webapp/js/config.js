var Tokens = [{"Name": "Google Account", "ID": 123}, {"Name":"Another Account 2", "ID": 232}];
var SelectedTokenID;
var PendingWatchUpdate = false;

var TokenByID = function(id) {
    for (var idx in Tokens) {
        if (Tokens[idx].ID == id) return Tokens[idx];
    }
};

var NextTokenID = function(){
    for (var id = 0; id < 256; id++) {
        if (!TokenByID(id)) return id;
    }
};

$(document).ready(function(e) {

    if (location.hash) {
        Tokens = JSON.parse(decodeURIComponent(location.hash.substring(1)));
    }

    $('.token-list-container').sortable({
            'tolerance': 'pointer',
            'containment': 'parent',
            'opacity': 0.6,
            update: function(event, ui) {
                // Refresh the Tokens list to match
                var newTokens = [];
                $(".token-list-container a").each(function(){
                    newTokens.push(TokenByID($(this).data("id")));
                });
                if (Tokens != newTokens) {
                    RefreshTokenList();
                    SetPendingWatchUpdate();
                    Tokens = newTokens;
                    
                }
            }
        });

    $("#token-detail").on("pagebeforeshow", function(){
        $("input#token-name").val(TokenByID(SelectedTokenID).Name);
    });

    $("#token-new").on("pagebeforeshow", function(){
        $("#token-new input[type='text']").val("");
    });

    $("#config-save-btn").bind("click", ConfigurationSave).hide();

    $("#token-create-btn").bind("click", TokenCreated);

    $("#new-token-secret").bind("keyup", function(e){
        if (e.keyCode == 13) {
            TokenCreated();
        }
    });

    $("#token-save-btn").bind("click", TokenSaved);
    $("#token-delete-btn").bind("click", TokenDeleted);

    $("#token-name").bind("keyup", function(e){
        if (e.keyCode == 13) {
            TokenSaved();
        }
    });

    RefreshTokenList();
});

var SetPendingWatchUpdate = function(){
    $("#config-save-btn").show()
}

var RefreshTokenList = function(){
    $(".token-list-container").empty();
    var has_tokens = false;
    for (var idx in Tokens) {
        var token_item = $("<li><a href=\"#token-detail\" class=\"ui-icon-gear\"></a></li>");
        $("a", token_item).data("id", Tokens[idx].ID);
        $("a", token_item).text(Tokens[idx]["Name"]).bind("click", TokenSelected);
        $(".token-list-container").append(token_item).listview("refresh");
        has_tokens = true;
    }
    $("#no-tokens-message").toggle(!has_tokens);
};

var TokenSelected = function(){
    SelectedTokenID = $(this).data("id");
};

var TokenSaved = function(){
    if (!$("#token-name").val()) {
        alert("You must enter a name for this token");
        return;
    }
    TokenByID(SelectedTokenID).Name = $("#token-name").val();
    RefreshTokenList();
    SetPendingWatchUpdate();
    window.location.hash = "";
};

var TokenDeleted = function(){
    if (!confirm("Are you sure?")) return;
    Tokens.splice(Tokens.indexOf(TokenByID(SelectedTokenID)), 1);
    RefreshTokenList();
    SetPendingWatchUpdate();
    window.location.hash = "";
};

var TokenCreated = function(e){
    var token = {
        "ID": NextTokenID(),
        "Name": $("#new-token-name").val(),
        "Secret": $("#new-token-secret").val()
    };
    if (!token.Name || !token.Secret) {
        alert("You must enter a name and secret for the new token");
        return;
    }
    Tokens.push(token);
    SetPendingWatchUpdate();
    RefreshTokenList();
    window.location.hash = "";
};

var ConfigurationSave = function(){
    window.location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(Tokens));
}