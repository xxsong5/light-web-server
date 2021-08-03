
/*
global variables for user's preferent status
*/

function Perference(jQuery){
    this.$=jQuery;

    this.auth = {};
    this.authsetted = false;
}

Perference.prototype = {

    userAuth: function(auth){
        this.auth = auth;
        this.authsetted = true;
    },

    valueGetSync : function(key){
        if (this.authsetted && (typeof key == "string")){

        }else if (typeof key == "string"){

        }

        console.info("editer Sync perference", key);
    },

    valueGetAync : function(key, onsuccess, onfailed){
        if (typeof onsuccess== "function"){

        }

        console.info("editer Async perference", key);
    },

    valueSetAync : function(key, val, onsuccess, onfailed){

        console.info("editer Async perference", key);
    }

};


var perference = new Perference($);
