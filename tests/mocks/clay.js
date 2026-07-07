function Clay(config, customFn, options) {
  this.config = config;
  this.customFn = customFn;
  this.options = options || {};
}
Clay.prototype.on = function() { return this; };
Clay.prototype.onOpen = function() { return this; };
Clay.prototype.onMessage = function() { return this; };
Clay.prototype.generateUrl = function() { return 'data:text/html,mock'; };
// Mirrors the real Clay: parses the response, persists a flattened copy to
// localStorage 'clay-settings', and returns raw settings when convert===false.
Clay.prototype.getSettings = function(response, convert) {
  var settings = JSON.parse(
    response.match(/^\{/) ? response : decodeURIComponent(response));
  var flattened = {};
  Object.keys(settings).forEach(function(key) {
    var value = settings[key];
    flattened[key] = (value && typeof value === 'object') ? value.value : value;
  });
  localStorage.setItem('clay-settings', JSON.stringify(flattened));
  return convert === false ? settings : Clay.prepareSettingsForAppMessage(settings);
};
Clay.prepareSettingsForAppMessage = function(settings) {
  var result = {};
  Object.keys(settings).forEach(function(key) {
    var value = settings[key];
    result[key] = (value && typeof value === 'object') ? value.value : value;
  });
  return result;
};
module.exports = Clay;
