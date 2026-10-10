const path = require('path');

const originalResolveFilename = require('module')._resolveFilename;
require('module')._resolveFilename = function(request, parent) {
  if (request === 'message_keys') {
    return path.resolve(__dirname, 'mocks/message_keys.js');
  }
  if (request === 'package.json') {
    return path.resolve(__dirname, 'mocks/package.json.js');
  }
  if (request === 'squire_telegram_app') {
    return path.resolve(__dirname, 'mocks/squire_telegram_app.js');
  }
  if (request === '@rebble/clay') {
    return path.resolve(__dirname, 'mocks/clay.js');
  }
  return originalResolveFilename.apply(this, arguments);
};