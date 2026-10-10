import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const location = require('../src/pkjs/location.js');
const config = require('../src/pkjs/config.js');

function enable(value) {
    config.setSetting('LOCATION_ENABLED', value);
    location.update();
}

describe('location permission', () => {
    let request;
    let success;

    beforeEach(() => {
        localStorage.clear();
        request = vi.fn(callback => { success = callback; });
        vi.stubGlobal('navigator', { geolocation: { getCurrentPosition: request } });
        enable(false);
    });

    afterEach(() => {
        enable(false);
        vi.unstubAllGlobals();
    });

    it('does not acquire or restore a location without permission', () => {
        localStorage.setItem('oldLon', '1');
        localStorage.setItem('oldLat', '2');
        location.update();
        expect(request).not.toHaveBeenCalled();
        expect(location.isReady()).toBe(false);
        expect(localStorage.getItem('oldLon')).toBe(null);
        expect(localStorage.getItem('oldLat')).toBe(null);
    });

    it('accepts coordinates on the equator and prime meridian', () => {
        enable(true);
        success({ coords: { latitude: 0, longitude: 0 } });
        expect(location.isReady()).toBe(true);
        expect(location.getPos()).toEqual({ lat: 0, lon: 0 });
    });

    it('discards an outstanding result after permission is withdrawn', () => {
        enable(true);
        const oldCallback = success;
        enable(false);
        oldCallback({ coords: { latitude: 51, longitude: -1 } });
        expect(location.isReady()).toBe(false);
        expect(localStorage.getItem('oldLat')).toBe(null);
        enable(true);
        expect(location.isReady()).toBe(false);
    });

    it('rejects invalid cached coordinates and unavailable geolocation', () => {
        vi.stubGlobal('navigator', {});
        localStorage.setItem('oldLat', 'NaN');
        localStorage.setItem('oldLon', '999');
        expect(() => enable(true)).not.toThrow();
        expect(location.isReady()).toBe(false);
    });

    it('keeps startup usable when geolocation throws', () => {
        request.mockImplementation(() => { throw new Error('Permission denied'); });
        expect(() => enable(true)).not.toThrow();
        expect(location.isReady()).toBe(false);
    });
});
