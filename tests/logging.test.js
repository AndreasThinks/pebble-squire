import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';

const fake = require('./helpers/fake_gramjs.js');
const { NewMessage, Raw, makeFakeClient, botMessage, peerUser } = require('./helpers/fake_telegram.js');
const Queue = require('../src/pkjs/lib/message_queue.js').Queue;
const telegram = require('../src/pkjs/telegram');
const sessionStore = require('../src/pkjs/telegram/session.js');
const clientModule = require('../src/pkjs/telegram/client.js');
const history = require('../src/pkjs/history.js');
const auth = require('../src/pkjs/telegram/auth.js');
const index = require('../src/pkjs/index.js');

const SECRETS = {
    prompt: 'PROMPT-SECRET-what is my bank balance',
    phone: '+15550001111',
    code: '97531',
    dm: 'UNRELATED-DM-SECRET',
    reply: 'BOT-REPLY-SECRET',
    historyText: 'HISTORY-SECRET-text',
    originalArgs: 'ORIGINAL-ARGS-SECRET',
    session: 'SESSION-STRING-SECRET',
    password: 'PASSWORD-SECRET',
};

async function settle() {
    for (let i = 0; i < 10; i++) await new Promise(r => setTimeout(r, 0));
}

describe('logs never contain user content or credentials', () => {
    let output;

    beforeEach(() => {
        output = [];
        for (const level of ['log', 'info', 'warn', 'error', 'debug']) {
            vi.spyOn(console, level).mockImplementation((...args) => {
                output.push(args.map(a => (typeof a === 'string' ? a : JSON.stringify(a))).join(' '));
            });
        }
        global.Pebble.sendAppMessage = vi.fn((msg, ok) => ok());
        Queue.reset();
        localStorage.clear();
        auth.clearPendingAuth();
    });

    afterEach(() => {
        require('../src/pkjs/session.js').cancelActiveSession();
        auth.clearPendingAuth();
        fake.uninstall();
        vi.restoreAllMocks();
        Queue.reset();
    });

    function assertClean() {
        const all = output.join('\n');
        for (const [name, secret] of Object.entries(SECRETS)) {
            expect(all, name).not.toContain(secret);
        }
    }

    it('when handling prompts, replies and unrelated DMs', async () => {
        const client = makeFakeClient();
        localStorage.setItem('clay-settings', JSON.stringify({ AGENT_TELEGRAM_USERNAME: '@SquireTestBot' }));
        vi.spyOn(telegram, 'hasSession').mockReturnValue(true);
        vi.spyOn(telegram, 'isAppConfigured').mockReturnValue(true);
        vi.spyOn(telegram, 'initClient').mockResolvedValue(true);
        vi.spyOn(telegram, 'getClient').mockReturnValue(client);
        vi.spyOn(telegram, 'getEventBuilders').mockReturnValue({ NewMessage, Raw });
        index.handleAppMessage({ payload: { PROMPT: SECRETS.prompt, REQUEST_ID: 9 } });
        await settle();
        client.emitNewMessage({ id: 101, message: SECRETS.dm, out: false, peerId: peerUser('999') });
        client.emitNewMessage(botMessage(102, SECRETS.reply + ' [done]'));
        await settle();
        expect(output.some(line => line.includes('PROMPT'))).toBe(true);  // key names are fine
        assertClean();
    });

    it('when handling watch sign-in messages', async () => {
        fake.install(vi, {
            onInvoke(req) {
                if (req.className === 'auth.SendCode') return Promise.resolve({ phoneCodeHash: 'H' });
                return Promise.reject(fake.telegramError('SESSION_PASSWORD_NEEDED'));
            },
        });
        index.handleAppMessage({ payload: { TELEGRAM_START_AUTH: SECRETS.phone } });
        await settle();
        index.handleAppMessage({ payload: { TELEGRAM_PROVIDE_CODE: SECRETS.code } });
        await settle();
        await auth.providePassword(SECRETS.password).catch(() => {});
        assertClean();
    });

    it('when loading history', async () => {
        localStorage.setItem('clay-settings', JSON.stringify({ AGENT_TELEGRAM_USERNAME: '@SquireTestBot' }));
        vi.spyOn(telegram, 'hasSession').mockReturnValue(true);
        vi.spyOn(telegram, 'isAppConfigured').mockReturnValue(true);
        vi.spyOn(telegram, 'initClient').mockResolvedValue(true);
        vi.spyOn(telegram, 'getClient').mockReturnValue({
            getEntity: () => Promise.resolve({ className: 'User', bot: true, id: 1 }),
            getMessages: () => Promise.resolve([
                { id: 2, out: false, message: SECRETS.historyText, originalArgs: { secret: SECRETS.originalArgs } },
                { id: 1, out: false, message: '', originalArgs: { secret: SECRETS.originalArgs } },
            ]),
        });
        await history.fetchAndSendHistory();
        assertClean();
    });

    it('when connecting with a stored session', async () => {
        fake.install(vi, {});
        sessionStore.saveSession(SECRETS.session);
        await clientModule.initClient();
        await clientModule.resetClient();
        assertClean();
    });
});
