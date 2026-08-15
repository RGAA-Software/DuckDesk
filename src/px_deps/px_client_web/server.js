'use strict'

var http = require('http');
var https = require('https');
var fs = require('fs');
var express = require('express');
var serveIndex = require('serve-index')

var app = express();
app.use(serveIndex('./'));
app.use(express.static('./'));

app.get('/handle', function(req, res) {
    res.send("This is root handler");
});

var httpServer = http.createServer(app);
httpServer.listen(80, '0.0.0.0');

var options = {
    key : fs.readFileSync('cert/syxmsg.xyz.key'),
    cert : fs.readFileSync('cert/syxmsg.xyz_bundle.pem')
};


var httpsServer = https.createServer(options, app);
httpsServer.listen(443, '0.0.0.0');