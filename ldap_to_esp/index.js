var ldap = require('ldapjs');
var util = require('util');
var _ = require('lodash');
var request = require('request');

var ldapurl = 'ldap://newldap.256.makerslocal.org';
var espurl = 'http://10.56.1.156/upload';
var basedn = 'ou=people,dc=makerslocal,dc=org';
var buffer = '';

function parse(user){

  if (!user.nfcID){
    return;
  }

  buffer += util.format('%s,%s\n', user.uid, _.concat(user.nfcID));

}

function send(){

  console.log(buffer);

  var options = {
        method: 'POST',
        url: espurl,
        headers:{
               // node makes all headers lowercase, esp does not like
               'Content-Length': buffer.length,
        },
        body: buffer };

  request(options, function (error, response, body) {
      if (error) {
        console.log(error);
      }
      else {
        console.log(response.statusCode);
        console.log(body);
      }
  });
}

var client = ldap.createClient({
  url: ldapurl
});

var opts = {
  filter: '(|(nfcID=*))',
  scope: 'sub',
  attributes: ['uid', 'nfcID'],
};

client.search(basedn, opts, function (err, res) {
    // get each user
    res.on('searchEntry', function (entry) {
      if (err){
        console.log(err);
      }
      else {
        parse(entry.object);
      }
    });

    // we have errors
    res.on('error', function(err) {
      console.error('error: ' + err.message);
    });

    // got all users
    res.on('end', function(result) {
      if ( result.status !== 0 ){
         console.log('ldap failed status: ' + result.status);
      }
      else {
        send();
        client.unbind();
      }
    });
});

