import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const fake = require('./helpers/fake_gramjs.js');
const Queue = require('../src/pkjs/lib/message_queue.js').Queue;
const sessionStore = require('../src/pkjs/telegram/session.js');
const auth = require('../src/pkjs/telegram/auth.js');

// Importing index.js registers the showConfiguration/webviewclosed handlers
// on the mocked Pebble object.
require('../src/pkjs/index.js');

const PHONE = '+15557654321';
const CODE = '13579';
const PASSWORD = 'tr0ub4dor&3';

function getEventHandler(name) {
    const calls = global.Pebble.addEventListener.mock.calls;
    for (let i = calls.length - 1; i >= 0; i--) {
        if (calls[i][0] === name) return calls[i][1];
    }
    return null;
}

async function settle() {
    for (let i = 0; i < 5; i++) await new Promise(r => setTimeout(r, 0));
}

describe('config page', () => {
    let sent;
    let gram;

    beforeEach(() => {
        sent = [];
        global.Pebble.sendAppMessage = vi.fn((msg, ok) => { sent.push(msg); ok(); });
        Queue.reset();
        localStorage.clear();
        localStorage.setItem.mockClear();
        auth.clearPendingAuth();
        gram = fake.install(vi, {
            onInvoke(req) {
                if (req.className === 'auth.SendCode') return Promise.resolve({ phoneCodeHash: 'H', type: { length: 5 } });
                return Promise.resolve({});
            },
        });
    });

    afterEach(() => {
        auth.clearPendingAuth();
        fake.uninstall();
        vi.restoreAllMocks();
        Queue.reset();
    });

    it('registers manual configuration handlers', () => {
        expect(getEventHandler('showConfiguration')).toBeTypeOf('function');
        expect(getEventHandler('webviewclosed')).toBeTypeOf('function');
    });

    it('never writes the code or password to storage, not even briefly', async () => {
        // Regression: Clay persisted every field to clay-settings and the
        // sign-in fields were scrubbed afterwards (write-then-delete).
        getEventHandler('webviewclosed')({ response: JSON.stringify({
            AGENT_TELEGRAM_USERNAME: { value: '@HermesHelperBot' },
            TELEGRAM_PHONE: { value: PHONE },
            TELEGRAM_CODE: { value: CODE },
            TELEGRAM_2FA_PASSWORD: { value: PASSWORD },
        }) });
        await settle();
        for (const [key, value] of localStorage.setItem.mock.calls) {
            expect(String(value)).not.toContain(CODE);
            expect(String(value)).not.toContain(PASSWORD);
            if (key === 'clay-settings') expect(String(value)).not.toContain(PHONE);
        }
        const stored = JSON.parse(localStorage.getItem('clay-settings'));
        expect(stored).toEqual({ AGENT_TELEGRAM_USERNAME: '@HermesHelperBot' });
        for (const msg of sent) {
            expect(JSON.stringify(msg)).not.toContain(CODE);
            expect(JSON.stringify(msg)).not.toContain(PASSWORD);
            expect(JSON.stringify(msg)).not.toContain(PHONE);
        }
    });

    it('accepts URI-encoded responses like Clay does', async () => {
        getEventHandler('webviewclosed')({ response: encodeURIComponent(JSON.stringify({
            CONFIRM_TRANSCRIPTS: { value: true },
            TELEGRAM_2FA_PASSWORD: { value: PASSWORD },
        })) });
        await settle();
        expect(JSON.parse(localStorage.getItem('clay-settings'))).toEqual({ CONFIRM_TRANSCRIPTS: true });
    });

    it('starts sign-in from a phone number and reports the code length', async () => {
        getEventHandler('webviewclosed')({ response: JSON.stringify({ TELEGRAM_PHONE: { value: PHONE } }) });
        await settle();
        expect(sent).toContainEqual({ TELEGRAM_CODE_SENT: 5 });
        expect(auth.getAuthState().isWaitingForCode).toBe(true);
    });

    it('reports an actionable reason when a code arrives with no pending sign-in', async () => {
        getEventHandler('webviewclosed')({ response: JSON.stringify({ TELEGRAM_CODE: { value: CODE } }) });
        await settle();
        expect(sent).toContainEqual({ TELEGRAM_AUTH_ERROR: auth.REASONS.NO_PENDING });
    });

    it('rejects an invalid phone number with a reason', async () => {
        getEventHandler('webviewclosed')({ response: JSON.stringify({ TELEGRAM_PHONE: { value: 'call me' } }) });
        await settle();
        expect(sent).toContainEqual({ TELEGRAM_AUTH_ERROR: auth.REASONS.INVALID_PHONE });
        expect(gram.instances.length).toBe(0);
    });

    it('stores an invalid bot username as empty instead of guessing', async () => {
        getEventHandler('webviewclosed')({ response: JSON.stringify({ AGENT_TELEGRAM_USERNAME: { value: 'my bot!' } }) });
        await settle();
        expect(JSON.parse(localStorage.getItem('clay-settings')).AGENT_TELEGRAM_USERNAME).toBe('');
        expect(sessionStore.getBotUsername()).toBe('');
    });

    it('does nothing auth-related when the response has no sign-in fields', async () => {
        getEventHandler('webviewclosed')({ response: JSON.stringify({ CONFIRM_TRANSCRIPTS: { value: true } }) });
        await settle();
        expect(sent.some(m => 'TELEGRAM_AUTH_ERROR' in m)).toBe(false);
        expect(sent.some(m => 'CONFIRM_TRANSCRIPTS' in m)).toBe(true);
    });
});
