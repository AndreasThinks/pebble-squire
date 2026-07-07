import { describe, it, expect, beforeEach } from 'vitest';
import 'dotenv/config';

// Importing index.js registers the showConfiguration/webviewclosed handlers
// on the mocked Pebble object.
import '../src/pkjs/index.js';

function getEventHandler(name) {
    const calls = global.Pebble.addEventListener.mock.calls;
    for (let i = calls.length - 1; i >= 0; i--) {
        if (calls[i][0] === name) return calls[i][1];
    }
    return null;
}

describe('config page auth handling', () => {
    beforeEach(() => {
        global.Pebble.sendAppMessage.mockClear();
        localStorage.removeItem('clay-settings');
        localStorage.removeItem('telegram_session');
    });

    it('registers manual configuration handlers', () => {
        expect(getEventHandler('showConfiguration')).toBeTypeOf('function');
        expect(getEventHandler('webviewclosed')).toBeTypeOf('function');
    });

    it('scrubs auth fields from clay-settings and the watch message', async () => {
        const handler = getEventHandler('webviewclosed');
        const response = JSON.stringify({
            AGENT_TELEGRAM_USERNAME: { value: '@MyBot' },
            TELEGRAM_PHONE: { value: '+15551234567' },
            TELEGRAM_CODE: { value: '12345' },
            TELEGRAM_2FA_PASSWORD: { value: 'hunter2' },
        });

        handler({ response });
        // Let the (failing, since no code was pending) auth flow settle.
        await new Promise(r => setTimeout(r, 50));

        const stored = JSON.parse(localStorage.getItem('clay-settings'));
        expect(stored.AGENT_TELEGRAM_USERNAME).toBe('@MyBot');
        expect(stored).not.toHaveProperty('TELEGRAM_PHONE');
        expect(stored).not.toHaveProperty('TELEGRAM_CODE');
        expect(stored).not.toHaveProperty('TELEGRAM_2FA_PASSWORD');

        for (const call of global.Pebble.sendAppMessage.mock.calls) {
            expect(call[0]).not.toHaveProperty('TELEGRAM_PHONE');
            expect(call[0]).not.toHaveProperty('TELEGRAM_CODE');
            expect(call[0]).not.toHaveProperty('TELEGRAM_2FA_PASSWORD');
        }
    });

    it('reports an auth error when a code is provided with no pending request', async () => {
        const handler = getEventHandler('webviewclosed');
        handler({ response: JSON.stringify({ TELEGRAM_CODE: { value: '12345' } }) });
        await new Promise(r => setTimeout(r, 50));

        const sent = global.Pebble.sendAppMessage.mock.calls.map(c => c[0]);
        expect(sent.some(m => m.TELEGRAM_AUTH_ERROR === 1)).toBe(true);
    });

    it('does nothing when the response has no auth fields', async () => {
        const handler = getEventHandler('webviewclosed');
        handler({ response: JSON.stringify({ CONFIRM_TRANSCRIPTS: { value: true } }) });
        await new Promise(r => setTimeout(r, 50));

        const sent = global.Pebble.sendAppMessage.mock.calls.map(c => c[0]);
        expect(sent.some(m => m.TELEGRAM_AUTH_ERROR === 1)).toBe(false);
        expect(sent.some(m => 'CONFIRM_TRANSCRIPTS' in m)).toBe(true);
    });
});
