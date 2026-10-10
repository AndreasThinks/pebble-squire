/**
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

var config = require('./config');
var cachedLon;
var cachedLat;
var generation = 0;

function validPosition(lat, lon) {
    return isFinite(lat) && isFinite(lon) &&
        lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}

exports.update = function() {
    var mine = ++generation;
    if (!config.isLocationEnabled()) {
        cachedLon = undefined;
        cachedLat = undefined;
        try {
            localStorage.removeItem('oldLon');
            localStorage.removeItem('oldLat');
        } catch (e) {}
        return;
    }

    try {
        var oldLon = localStorage.getItem('oldLon');
        var oldLat = localStorage.getItem('oldLat');
        var lon = Number(oldLon);
        var lat = Number(oldLat);
        if (oldLon !== null && oldLat !== null && oldLon !== '' && oldLat !== '' && validPosition(lat, lon)) {
            cachedLon = lon;
            cachedLat = lat;
        }
    } catch (e) {}

    // Some phone/emulator runtimes do not provide geolocation.
    if (typeof navigator === 'undefined' || !navigator.geolocation ||
        typeof navigator.geolocation.getCurrentPosition !== 'function') {
        return;
    }
    try {
        navigator.geolocation.getCurrentPosition(function(pos) {
            // Disabling location while a request is pending must also prevent
            // its eventual result from being cached or used by a prompt.
            if (mine !== generation || !config.isLocationEnabled() || !pos || !pos.coords ||
                !validPosition(pos.coords.latitude, pos.coords.longitude)) {
                return;
            }
            cachedLat = pos.coords.latitude;
            cachedLon = pos.coords.longitude;
            console.log('Position updated');
            try {
                localStorage.setItem('oldLon', String(cachedLon));
                localStorage.setItem('oldLat', String(cachedLat));
            } catch (e) {}
        }, function(err) {
            console.log('Failed to update location: ' + (err && err.code));
        }, {
            enableHighAccuracy: true,
            maximumAge: 300000,
            timeout: 30000
        });
    } catch (e) {
        console.log('Location is unavailable');
    }
};

exports.isReady = function() {
    return config.isLocationEnabled() && cachedLon !== undefined && cachedLat !== undefined;
};

exports.getPos = function() {
    return { lon: cachedLon, lat: cachedLat };
};
